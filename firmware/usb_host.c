// Initialize and set registers needed to control USB selection and status.
#include "hardware/gpio.h"
#include "pico/binary_info.h"

// This file hold all the handling of the filesystem of the connected device.
#include "tusb_option.h"

#include "pico/multicore.h"

#include "pio_usb.h"
#include "host/usbh.h" // Config ID for tuh_config.

// Needed to transfer files.
#include "class/msc/msc_host.h"

// Needed to force devices to switch to BOOTSEL mode.
#include "class/cdc/cdc_host.h"

// FatFS header, to implement anything which is related to the handling of FAT
// file systems, in order to write a bunch of files at the root of the file
// system.
#include "ff.h"

// FatFS Diskio header which define the set of functions which have to be
// implemented to make use of FatFS library. These function interface fatfs
// implementation with the primitives provided by TinyUSB Host MSC primitives.
#include "diskio.h"

// Collect references to callback tasks.
#include "web_server.h"

// Handle queuing tasks across CPU cores managing either USB host or the Web
// server.
#include "pipe.h"

// Some debugging
#include "input.h"

#include "usb_host.h"

#define $UNUSED __attribute__((__unused__))

//---------------------------------------------------------------------
// Manage and record status of USB connections.
// Enable and disable select and enable pins.

// PIO emulated USB port.
static const uint PIN_USB_DP = 0;
static const uint PIN_USB_DM $UNUSED = 1;

// Port selection bits.
static const uint PIN_SEL0 = 2;
static const uint PIN_SEL1 = 3;
static const uint PIN_SEL2 = 4;
static const uint PIN_SEL3 = 5;
static const uint PIN_SEL4 = 6;
static const uint PIN_SEL5 = 7;

// Enable pins.
//
// Note the power pins should always be on while the data pins are connected.
// The USB spec has specifically designed connectors to have shorter data pins,
// such that power can flow to the connected device such that they can properly
// answer on the data pins once they are connected.
static const uint PIN_ENABLE_DATA = 8;
static const uint PIN_ENABLE_POWER = 9;

void init_select_pin(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  gpio_pull_down(pin);
  gpio_put(pin, false);
}

void init_enable_pin(uint pin) {
  gpio_init(pin);
  gpio_set_dir(pin, GPIO_OUT);
  // Set the enable pin as not enabling anything yet.
  // The logic of the component is inverted.
  gpio_pull_up(pin);
  gpio_put(pin, false);
}

void usb_gpio_init() {
  bi_decl_if_func_used(bi_program_feature("Select USB device"));
  init_select_pin(PIN_SEL0);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL0, "S0"));
  init_select_pin(PIN_SEL1);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL1, "S1"));
  init_select_pin(PIN_SEL2);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL2, "S2"));
  init_select_pin(PIN_SEL3);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL3, "S3"));
  init_select_pin(PIN_SEL4);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL4, "S4"));
  init_select_pin(PIN_SEL5);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_SEL5, "S5"));

  bi_decl_if_func_used(bi_program_feature("Toggle USB device"));
  init_enable_pin(PIN_ENABLE_DATA);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_ENABLE_DATA, "EN_Data"));
  init_enable_pin(PIN_ENABLE_POWER);
  bi_decl_if_func_used(bi_1pin_with_name(PIN_ENABLE_POWER, "EN_Power"));

  printf("USB GPIO initialized!\n");
}

// Aggregate the abstract status of all devices.
static usb_status_t usb_status[USB_DEVICES];

// Index of the active device, if none, then this is equal to USB_DEVICES.
size_t active_device = USB_DEVICES;

void report_status(usb_status_t st) {
  if (active_device >= USB_DEVICES) {
    return;
  }
  usb_status_t status = usb_status[active_device];
  status = (status & DEVICE_IS_MOUNTED) | st;
  usb_status[active_device] = status;
  //printf("usb[%d] = %x\n", active_device, status);
}

void set_mount_status(usb_status_t st, bool set) {
  if (active_device >= USB_DEVICES) {
    return;
  }
  if (set) {
    usb_status[active_device] |= st;
  } else {
    usb_status[active_device] &= ~st;
  }
  printf("usb[%d] = %x\n", active_device, usb_status[active_device]);
}

void reset_status() {
  usb_status[active_device] = DEVICE_UNKNOWN;
  printf("usb[%d] = %x\n", active_device, usb_status[active_device]);
}

void reset_usb_status() {
  // By default we do not know anything about any of the plugged devices.
  for(size_t d = 0; d < USB_DEVICES; d++) {
    usb_status[d] = DEVICE_UNKNOWN;
  }
  printf("Reset all USB status\n");
}

usb_status_t get_usb_device_status(size_t d) {
  if (d >= USB_DEVICES) {
    return DEVICE_UNKNOWN;
  }
  return usb_status[d];
}

void disable_usb_power() {
  gpio_put(PIN_ENABLE_POWER, true);
}

void enable_usb_power() {
  gpio_put(PIN_ENABLE_POWER, false);
}

void disable_usb_data() {
  gpio_put(PIN_ENABLE_DATA, true);
}

void enable_usb_data() {
  gpio_put(PIN_ENABLE_DATA, false);
}

void select_device(size_t device) {
  // Disconnect data and power pin of the device.
  if (active_device < USB_DEVICES) {
    printf("Disconnect USB %d Data.\n", active_device);
    disable_usb_data();
    sleep_ms(1);
    printf("Disconnect USB %d Power.\n", active_device);
    disable_usb_power();
    sleep_ms(1);

    // Wait until TinyUSB reports the disk as unmounted, if it were ever
    // mounted.
    if (usb_status[active_device] & DEVICE_IS_MOUNTED) {
      printf("Wait for USB %d to be unmounted.\n", active_device);
    }
    while (usb_status[active_device] & DEVICE_IS_MOUNTED) {
      tuh_task();
    }

    // Clear all pins used for selecting a device.
    const uint select_mask =
      (1 << PIN_SEL0) |
      (1 << PIN_SEL1) |
      (1 << PIN_SEL2) |
      (1 << PIN_SEL3) |
      (1 << PIN_SEL4) |
      (1 << PIN_SEL5);
    gpio_clr_mask(select_mask);
  }

  active_device = device;

  // Connect the power and data pins of the selected device.
  if (active_device < USB_DEVICES) {
    printf("Select USB device: %d\n", active_device);
    reset_status();

    // Set all pins used for selecting a device.
    const uint select_mask =
      (active_device & 0x01 ? 1u << PIN_SEL0 : 0) |
      (active_device & 0x02 ? 1u << PIN_SEL1 : 0) |
      (active_device & 0x04 ? 1u << PIN_SEL2 : 0) |
      (active_device & 0x08 ? 1u << PIN_SEL3 : 0) |
      (active_device & 0x10 ? 1u << PIN_SEL4 : 0) |
      (active_device & 0x20 ? 1u << PIN_SEL5 : 0);
    gpio_set_mask(select_mask);

    sleep_ms(1);
    enable_usb_power();
    sleep_ms(1);
    enable_usb_data();
  }
}

void select_device_cb(void* arg) {
  //led_put(true);
  size_t device = (size_t) (uintptr_t) arg;
  if (device > USB_DEVICES) {
    device = USB_DEVICES;
  }
  select_device(device);
  //led_put(false);
}

void test_usb_power() {
  for (size_t i = 0; i < USB_DEVICES; i++) {
    sleep_ms(100);
    select_device(i);
  }
  select_device(USB_DEVICES);
}

//---------------------------------------------------------------------
// FatFS diskio implementation
// See tinyusb/lib/fatfs/source/diskio.h

// Record the mounted file systems for each device.
static FATFS fatfs[CFG_TUH_DEVICE_MAX]; // for simplicity only support 1 LUN per device
static FIL file[CFG_TUH_DEVICE_MAX];
static volatile bool tuh_disk_busy[CFG_TUH_DEVICE_MAX];

// Callback used by `disk_read` and `disk_write` to prevent multiple TinyUSB
// operation from overlapping each others.
//
// This unset the disk busy flag which is checked by wait_for_disk_io.
static bool disk_io_complete(uint8_t dev_addr, tuh_msc_complete_data_t const * cb_data)
{
  (void) cb_data;
  tuh_disk_busy[dev_addr - 1] = false;
  report_status(DEVICE_FLASH_DISK_IO_COMPLETE);
  return true;
}

// Wait for the disk_io_complete to be called. Note that we do not execute
// anything else than `tuh_task()` as `exec_usb_task` might push more operations
// while the current transaction has not ended yet.
static void wait_for_disk_io(BYTE pdrv)
{
  while(tuh_disk_busy[pdrv]) {
    tuh_task();
  }
}

// Required by `mount_volume`.
DSTATUS disk_initialize(BYTE pdrv)
{
  (void) pdrv;
  report_status(DEVICE_FLASH_DISK_INIT);
	return 0;
}

// Check whether the physical drive is accessible.
DSTATUS disk_status(BYTE pdrv)
{
  uint8_t dev_addr = pdrv + 1;
  return tuh_msc_mounted(dev_addr) ? 0 : STA_NODISK;
}

// Read from a physical drive `pdrv`, read a few contiguous sectors, starting at
// `sector` for `count` number of sectors and write it back into `buff`.
DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count)
{
  report_status(DEVICE_FLASH_DISK_READ_BUSY);

	uint8_t const dev_addr = pdrv + 1;
	uint8_t const lun = 0;
	tuh_disk_busy[pdrv] = true;
	tuh_msc_read10(dev_addr, lun, buff, sector, (uint16_t) count, disk_io_complete, 0);
	wait_for_disk_io(pdrv);
	return RES_OK;
}

// Write to a physical drive `pdrv`, write a few contiguous sectors, starting at
// `sector` for `count` number of sectors and read it from `buff`.
DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count)
{
  report_status(DEVICE_FLASH_DISK_WRITE_BUSY);

	uint8_t const dev_addr = pdrv + 1;
	uint8_t const lun = 0;
	tuh_disk_busy[pdrv] = true;
	tuh_msc_write10(dev_addr, lun, buff, sector, (uint16_t) count, disk_io_complete, 0);
	wait_for_disk_io(pdrv);
	return RES_OK;
}

// Generic command query about the physical drive `pdrv`. Each command `cmd` use
// the `buff` content to send/receive control data.
DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff)
{
  uint8_t const dev_addr = pdrv + 1;
  uint8_t const lun = 0;
  switch (cmd)
  {
  case CTRL_SYNC:
    // disk_read and disk_write are waiting for the completion of I/O
    // operation, thus all operations are already synchronous.
    return RES_OK;

  case GET_SECTOR_COUNT:
    *((DWORD*) buff) = (WORD) tuh_msc_get_block_count(dev_addr, lun);
    return RES_OK;

  case GET_SECTOR_SIZE:
    *((WORD*) buff) = (WORD) tuh_msc_get_block_size(dev_addr, lun);
    return RES_OK;

  case GET_BLOCK_SIZE:
    // erase block size in units of sector size
    *((DWORD*) buff) = 1;
    return RES_OK;

  case CTRL_TRIM:
#if FF_USE_TRIM == 1
# error "disk_ioctl: CTRL_TRIM is not implemented yet"
#endif

  default:
    // Only the command above are expected to be used by FatFS library in the
    // context of USB devices.
    //
    // TODO: Report some error back to the web server.
    return RES_PARERR;
  }

	return RES_OK;
}

//---------------------------------------------------------------------
// TinyUSB MSC callbacks

// Contains information about the connected device, such as the vendor, product
// and device class. This is useful for resetting the MCU into flashable mode.
// Such as setting the baud rate to 1200 on a Raspberry Pi Pico.
//static tusb_desc_device_t desc_device;

static scsi_inquiry_resp_t inquiry_resp;

bool inquiry_complete_cb(uint8_t dev_addr,
                         tuh_msc_complete_data_t const* cb_data)
{
  msc_cbw_t const* cbw = cb_data->cbw;
  msc_csw_t const* csw = cb_data->csw;

  if (csw->status != 0)
  {
    puts("Inquiry failed\n");
    report_status(DEVICE_ERROR_FLASH_INQUIRY);
    return false;
  }

  // Print out Vendor ID, Product ID and Rev
  printf("Vendor: %.8s\nProduct: %.16s\nRev: %.4s\n", inquiry_resp.vendor_id,
         inquiry_resp.product_id, inquiry_resp.product_rev);

  // Get capacity of device
  uint32_t const block_count = tuh_msc_get_block_count(dev_addr, cbw->lun);
  uint32_t const block_size = tuh_msc_get_block_size(dev_addr, cbw->lun);

  printf("Disk Size: %lu MB\nBlock Count: %lu\nBlock Size: %lu\n",
         block_count / ((1024*1024)/block_size), block_count,
         block_size);

  // For simplicity: we only mount 1 LUN per device
  uint8_t const drive_num = dev_addr - 1;
  char drive_path[3] = "0:";
  drive_path[0] += drive_num;

  if (f_mount(&fatfs[drive_num], drive_path, 1) != FR_OK) {
    report_status(DEVICE_ERROR_FLASH_MOUNT);
    puts("mount failed\n");
    return false;
  }

  // change to newly mounted drive
  f_chdir(drive_path);

  // Trigger notification of the Web client to ask it to send the file content.
  // Note, the web client is polling frequently the status and updating the
  // status would trigger the streaming of the image to be flashed.
  report_status(DEVICE_FLASH_REQUEST);
  queue_web_task(&request_flash, (void*) (uintptr_t) drive_num);

  return true;
}

void open_file(void* arg)
{
  // TODO: We should somehow get the filename across the web server to here, in
  // order to flash files with the proper name. For example, we do not want to
  // be flashing *.py files as *.uf2 files.
  uint8_t const drive_num = (uint8_t) (uintptr_t) arg;
  char file_path[13] = "0:/image.uf2";
  file_path[0] += drive_num;

  if (f_open(&file[drive_num], file_path, FA_WRITE | FA_CREATE_ALWAYS) != FR_OK) {
    report_status(DEVICE_ERROR_FLASH_OPEN);
    queue_web_task(&write_error, arg);
    return;
  }
}

void write_file_content(void* arg)
{
  uint8_t const drive_num = (uint8_t) (uintptr_t) arg;

  // UF2 images are flashed by multiple of 512 bytes.
  UINT count = 512;
  uint8_t buf[512];
  pipe_dequeue(&buf[0], sizeof(buf));

  if (f_write(&file[drive_num], buf, count, &count) != FR_OK) {
    report_status(DEVICE_ERROR_FLASH_WRITE);
    queue_web_task(&write_error, arg);
    return;
  }
}

void close_file(void* arg)
{
  uint8_t const drive_num = (uint8_t) (uintptr_t) arg;

  if (f_close(&file[drive_num]) != FR_OK) {
    report_status(DEVICE_ERROR_FLASH_CLOSE);
    queue_web_task(&write_error, arg);
    return;
  }
  report_status(DEVICE_FLASH_COMPLETE);
}

// The file system is mounted.
void tuh_msc_mount_cb(uint8_t dev_addr)
{
  printf("tuh_msc_mount_cb: %u\n", dev_addr);
  set_mount_status(DEVICE_MSC_MOUNTED, true);

  // Query information about the filesystem of the device, and mount it using
  // f_mount before manipulating it.
  uint8_t const lun = 0;
  tuh_msc_inquiry(dev_addr, lun, &inquiry_resp, inquiry_complete_cb, 0);
}

// The file system is unmounted.
void tuh_msc_unmount_cb(uint8_t dev_addr)
{
  printf("tuh_msc_unmount_cb: %u\n", dev_addr);
  set_mount_status(DEVICE_MSC_MOUNTED, false);

  uint8_t const drive_num = dev_addr - 1;
  char drive_path[3] = "0:";
  drive_path[0] += drive_num;
  f_unmount(drive_path);
}

//---------------------------------------------------------------------
// TinyUSB CDC callbacks

void baud_rate_set_cb(struct tuh_xfer_s* unused) {
  (void) unused;
}

void force_unmount_cdc(void* arg) {
  (void) arg;
  report_status(DEVICE_BOOTSEL_COMPLETE);

  // Disable data pins, and reenable data pins once the cdc_umount callback is
  // registered. Disabling is used to work-around an issue where the host
  // remains in a CDC state instead of unmounting the device.
  printf("Disable data to umount CDC\n");
  disable_usb_data();

  // Note we could explcitly call the cdch_close function with the device
  // address 1 to explicitly call the umount function. However, we should only
  // do this once we are no longer waiting for resposnes from the device, which
  // is controlled within TinyUSB library. Toggling the pins does that for us.
  //cdch_close(1);
}

void select_bootsel(void* arg) {
  uint8_t idx = (uint8_t) (uintptr_t) arg;

  // If reached, then set the baud rate such that if this is a Raspberry PI Pico
  // (RP2040), then the switch of the baud rate will reset the board in bootset
  // mode. Making the board open as a mass storage class device, ready to accept
  // a uf2 image.
  report_status(DEVICE_BOOTSEL_REQUEST);
  printf("Set BAUD rate to 1200, to switch to BOOTSEL mode (%u)\n", idx);
  cdc_line_coding_t line_coding = {
    1200, // Special value used by RPi Pico to reset to BOOTSEL mode.
    CDC_LINE_CONDING_STOP_BITS_1,
    CDC_LINE_CODING_PARITY_NONE,
    8
  };

  // As the device reboots after receiving the baud rate changes, the host stall
  // at waiting for an answer and this would cause an assertion failure (which
  // does not break the execution)
  tuh_cdc_set_line_coding(idx, &line_coding, baud_rate_set_cb, 0);
  queue_usb_task(&force_unmount_cdc, (void*) (uintptr_t) idx);
}

void restore_usb_data(void* arg) {
  (void) arg;
  enable_usb_data();
}

void tuh_cdc_mount_cb(uint8_t idx)
{
  printf("tuh_cdc_mount_cb: %u\n", idx);
  set_mount_status(DEVICE_CDC_MOUNTED, true);
  queue_usb_task(&select_bootsel, (void*) (uintptr_t) idx);
}

// Weakly linked, thus not causing errors if undefined.
void tuh_cdc_umount_cb(uint8_t idx) {
  printf("tuh_cdc_umount_cb: %u\n", idx);
  set_mount_status(DEVICE_CDC_MOUNTED, false);
  queue_usb_task(&restore_usb_data, (void*) (uintptr_t) idx);
}

//---------------------------------------------------------------------
// TinyUSB callbacks

// The device dev_addr is now plugged in.
void tuh_mount_cb(uint8_t dev_addr)
{
  printf("A device with address %d is mounted\r\n", dev_addr);
  set_mount_status(DEVICE_TUH_MOUNTED, true);

  // TODO: Turn on the notification LED from the Raspberry PI Pico.

  // TODO: Query information about the connected device.
  //tuh_descriptor_get_device(daddr, &desc_device, 18, print_device_descriptor, 0);
}

// The device dev_addr is now unplugged.
void tuh_umount_cb(uint8_t dev_addr)
{
  // application tear-down
  printf("A device with address %d is unmounted \r\n", dev_addr);
  set_mount_status(DEVICE_IS_MOUNTED, false);

  // TODO: Turn off the notification LED from the Raspberry PI Pico.
}

void usb_host_loop() {
  while(true) {
    // TinyUSB Host tasks.
    tuh_task();

    // Task requested by the web server.
    exec_usb_task();
  }
}

// To do it properly we should rely on atomics, but updating a single volatile
// boolean works as well as the volatile aspect implies that it cannot be
// aliased.
static semaphore_t usb_host_initialized;

void usb_host_main() {
  usb_gpio_init();
  reset_usb_status();
  test_usb_power();
  sleep_ms(10);

  bi_decl_if_func_used(bi_program_feature("USB host"));
  pio_usb_configuration_t pio_cfg = PIO_USB_DEFAULT_CONFIG;
  // NOTE: PIO_USB_DEFAULT_CONFIGURATION uses PIO_USB_DP_PIN_DEFAULT which
  // coincidentally happen to be the same as PIN_USB_DP, but we keep the
  // following line in case we want to setup a different pin.
  pio_cfg.pin_dp = PIN_USB_DP;
  bi_decl_if_func_used(bi_2pins_with_names(PIN_USB_DP, "USB Host D+", PIN_USB_DM, "USB Host D-"));

  // Initialize TinyUSB Host stack.
  if (!tuh_configure(BOARD_TUH_RHPORT, TUH_CFGID_RPI_PIO_USB_CONFIGURATION, &pio_cfg)) {
    printf("TinyUSB failed to configure PIO USB port.\n");
    return;
  }
  printf("TinyUSB Configured.\n");
  sleep_ms(10);
  tuh_init(BOARD_TUH_RHPORT);
  printf("TinyUSB Host port initialized.\n");

  // Inform the core-0 that core1 initialization is complete.
  sem_release(&usb_host_initialized);

  // Jump into the host task, and never return...
  printf("Starting USB Host loop.\n");
  usb_host_loop();
}

void usb_host_setup() {
  sem_init(&usb_host_initialized, 0, 1);

  multicore_reset_core1();
  multicore_launch_core1(usb_host_main);

  // Block until TinyUSB and Pico PIO USB have completed the setup of the USB on
  // Pin 0 and 1.
  while (!sem_acquire_timeout_ms(&usb_host_initialized, 60000)) {
    printf("Waiting for USB Host initialization.\n");
  }
  printf("USB Host initialized on core 1.\n");
}
