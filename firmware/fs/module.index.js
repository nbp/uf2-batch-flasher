// import * as jsBinding from 'importmap name';

// Display the output produced by the board and the output produced by this
// script on the console panel displayed on the page.
async function update_stdout() {
  let out = await fetch("/stdout.ssi");
  out = await out.text();
  let console = document.getElementById("console");
  if (out == "\n") return;
  let text = out.split('\n').map(line => `pico: ${line}\n`).join("");
  console.innerText += text;
}
function console_log(...args) {
  let console = document.getElementById("console");
  console.innerText += "web: " + args.map(a => a.toString()).join(" ") + "\n";
}

function asyncTimeout(timeout) {
  return new Promise((resolve, reject) => {
    setTimeout(reject, timeout);
  });
}

// Keep this list in sync with usb_host.h
let usb_status = [
  "DEVICE_UNKNOWN",
  "DEVICE_BOOTSEL_REQUEST",
  "DEVICE_BOOTSEL_COMPLETE",
  "DEVICE_FLASH_DISK_INIT",
  "DEVICE_FLASH_DISK_READ_BUSY",
  "DEVICE_FLASH_DISK_WRITE_BUSY",
  "DEVICE_FLASH_DISK_IO_COMPLETE",
  "DEVICE_FLASH_REQUEST",
  "DEVICE_FLASH_COMPLETE",
  "???", "???", "???", "???", "???", "???", "???",
  "DEVICE_ERROR_BOOTSEL_MISS",
  "DEVICE_ERROR_FLASH_INQUIRY",
  "DEVICE_ERROR_FLASH_MOUNT",
  "DEVICE_ERROR_FLASH_OPEN",
  "DEVICE_ERROR_FLASH_WRITE",
  "DEVICE_ERROR_FLASH_CLOSE",
  "DEVICE_DISCONNECTED",
  "???", "???", "???", "???", "???", "???", "???",
  "???", "???"
];

const USB_DEVICES = 64;

// Function which resolves or rejects a promise if the condition is met.
let last_status = [];
let wait_for_status = [];
async function update_status(status) {
  if (!status) {
    status = await fetch("/status.json");
  }

  status = await status.json();

  let dom = document.getElementById("usb_status_list");
  while (dom.firstElementChild) {
    dom.removeChild(dom.firstElementChild);
  }
  for (let s of status) {
    let li = document.createElement("li");
    let text = usb_status[s & 0x1f] ?? "???";
    text += " (0x" + s.toString(16) + ")";
    li.innerText = text;
    dom.appendChild(li);
  }

  wait_for_status = wait_for_status.filter(query => !query(status));
  last_status = status;
  return status;
}

async function wait_for_usb_status(device, expected_status, timeout, msg) {
  let no_more_checks = false;
  let wait = new Promise((resolve, reject) => {
    function check_for_expectation(status) {
      //console_log(`Check usb ${device} status (=${status[device].toString(16)}) for ${expected_status.toString(16)}`);
      if (no_more_checks) {
        reject(status[device]);
        return true;
      }

      // Reject the promise in case of error.
      if (status[device] & 0x10) {
        reject(status[device]);
        return true;
      }

      if (expected_status >= 0x20) {
        // Wait until the device is mounted.
        if (status[device] & expected_status == expected_status) {
          resolve(status[device]);
          return true;
        }
      } else {
        // Wait until a state is reached.
        if ((status[device] & 0x0f) >= expected_status) {
          resolve(status[device]);
          return true;
        }
      }
      return false;
    };

    if (!check_for_expectation(last_status)) {
      wait_for_status.push(check_for_expectation);
    }
  });

  try {
    await Promise.race([wait, asyncTimeout(timeout)]);
  } catch (e) {
    no_more_checks = true;
    if (e === undefined) {
      throw new Error(msg);
    } else {
      throw new Error(`Unexpected status code: 0x${e.toString(16)}`);
    }
  }
}

async function select_device(device, cdc_timeout, msc_timeout) {
  await update_status(await fetch(`/select.cgi?active_device=${device}`));

  if (device >= USB_DEVICES) {
    return;
  }

  if (cdc_timeout == 0 || msc_timeout == 0) {
    return;
  }

  // Note, in case of MSC device, the wait would be removed as the received code
  // is higher than the CDC code.
  await wait_for_usb_status(
    device, 0x01, cdc_timeout, "Timeout while waiting for BOOTSEL request");
  await wait_for_usb_status(
    device, 0x02, cdc_timeout, "Timeout while waiting for BOOTSEL mode");

  // let wait_for_msc_mounted = wait_for_usb_status(
  //   device, 0x40, timeout, "Timeout while waiting for USB Mass Storage Class");
  // await wait_for_msc_mounted;

  let flash_request = wait_for_usb_status(
    device, 0x07, msc_timeout, "Timeout while waiting for flash request");
  await flash_request;
}

let range_min = 0;
let range_max = USB_DEVICES;
function set_usb_range(min, max) {
  range_min = min;
  range_max = max;
}

let cdc_timeout = 2000;
let msc_timeout = 30000;
let flash_timeout = 120000;

// content is an array buffer, typed array, blob, json or text.
async function send_uf2(name, content) {

  for (let device = range_min; device < range_max; device++) {
    try {
      // Request to switch to the next flashable USB port. the reply from the
      // Pico would tell us whether to send or not the uf2 image again.
      await select_device(device, cdc_timeout, msc_timeout);

      // Make a single request which would be split into multiple by TCP
      // protocol and then throttled by LwIP based on how fast we can forward
      // the content to the USB device.
      console_log(`Flashing content: ${content.byteLength} bytes to flash.`);
      await update_status(await fetch("/flash", {
        method: "POST",
        mode: "same-origin",
        cache: "no-cache",
        credentials: "same-origin",
        headers: {
          "Content-Type": "application/uf2",
          "Content-Length": content.byteLength,
        },
        body: content
      }));

      // Flashing the device takes time, and the previous request only completes
      // once the POST buffer is full, which only implies that everything has
      // been queued, not flashed. Wait 500ms at most before assuming that an
      // unreported error occured.
      let wait_flash_complete = wait_for_usb_status(
        device, 0x08, flash_timeout, "Timeout while waiting for flash complete");
      await wait_flash_complete;
    } catch(e) {
      console_log(`Unable to flash device at USB port ${device}:\n${e}`);
    }
  }

  // Unpower all USB devices after having iterated over all of them. Otherwise
  // the last USB port might remain connected.
  select_device(USB_DEVICES, 0);
}

let flash_all_click_handler = null;
async function dropFilesHandler(ev) {
  console_log("File(s) dropped:");
  ev.preventDefault();

  let flashAll = document.getElementById("flash_all");
  if (flash_all_click_handler) {
    flashAll.removeEventListener("click", flash_all_click_handler);
    flashAll.disabled = true;
  }
  flash_all_click_handler = null;

  let dataTransfer = ev.dataTransfer;
  console.log([...dataTransfer.files].map(file => file.name));
  if ([...dataTransfer.files].length > 1) {
    console_log("TODO: Multiple files are not supported yet.");
  }

  let filesContent = [];
  for (let file of dataTransfer.files) {
    let buffer = await file.arrayBuffer();
    // UF2 files are divided in fixed sized chunks starting with 'UF2\n'
    //let header = String.fromCharCode(...new Uint8Array(buffer.slice(0, 4)));
    filesContent.push({
      name: file.name,
      content: buffer
    });
  };

  async function clickHandler(ev) {
    console_log("Flashing all files.");
    ev.preventDefault();
    if (flashAll.disabled) {
      return;
    }

    // Disable the button while we are flashing usb devices.
    flashAll.disabled = false;
    for (let file of filesContent) {
      await send_uf2(file.name, file.content);
    }
    flashAll.disabled = true;
  }

  // Update the button to flash USB devices using the files of the data
  // transfer.
  flash_all_click_handler = clickHandler;
  flashAll.addEventListener("click", flash_all_click_handler);
  flashAll.disabled = false;
}

function dropDragOver(ev) {
  // Needed to avoid seeing null dataTransfer field while processing ondrop
  // events.
  ev.preventDefault();
}

let status_timer = null;
let stdout_timer = null;

// Hook the current script and attach it to the DOM.
function setup() {
  // Register an action when new files are selected.
  let dropzone = document.getElementById("dropzone");
  dropzone.addEventListener("drop", dropFilesHandler);
  dropzone.addEventListener("dragover", dropDragOver);

  // Poll the Pico every 500ms to collect new status information about the USB
  // devices. This is useful to unlock promises which are waiting for changes in
  // the state of USB devices.
  status_timer = setInterval(update_status, 245, undefined);
  stdout_timer = setInterval(update_stdout, 1000, undefined);
}

function unsetup() {
  let dropzone = document.getElementById("dropzone");
  dropzone.removeEventListener("drop", dropFilesHandler);
  dropzone.removeEventListener("dragover", dropDragOver);

  clearInterval(status_timer);
  clearInterval(stdout_timer);

  let flashAll = document.getElementById("flash_all");
  if (flash_all_click_handler) {
    flashAll.removeEventListener("click", flash_all_click_handler);
    flashAll.disabled = true;
  }
  flash_all_click_handler = null;
}

setup();

window.update_stdout = update_stdout;
window.update_status = update_status;
window.setup = setup;
window.unsetup = unsetup;
window.set_usb_range = set_usb_range;
window.break_in_module = break_in_module;
export {
  update_stdout,
  update_status,
  setup,
  unsetup,
  set_usb_range
}
