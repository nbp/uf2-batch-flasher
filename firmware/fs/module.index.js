// import * as jsBinding from 'importmap name';

function Mutex() {
  return {
    turn: Promise.resolve(),
    lock: async function lock() {
      let turn = this.turn;
      let unlock;
      this.turn = new Promise(res => { unlock = res; });
      await turn;
      return unlock;
    }
  };
}

// Prevent multiple fetch calls from happening at the same time.
let fetch_mutex = Mutex();

// Start logging fetch calls if any fetch takes more than this threshold.
let logging_threshold = 600; // ms

// The Pico might have hard time at replying to multiple requests at the same
// time, thus we queue requests and execute them one at a time.
async function queued_fetch(...args) {
  let queued = performance.now();
  let unlock = await fetch_mutex.lock();
  let start = performance.now();
  let logging_unlock = () => {
    let qtime = start - queued;
    let delta = performance.now() - start;
    if (delta > logging_threshold || qtime > logging_threshold) {
      console_log(`fetch(${args[0]}) {queued for ${qtime} ms, took ${delta} ms}`);
    }
    unlock();
  };
  return { fetch: fetch(...args), unlock: logging_unlock };
}

// Display the output produced by the board and the output produced by this
// script on the console panel displayed on the page.
async function update_stdout() {
  let out, unlock;
  try {
    let queue = await queued_fetch("/stdout.ssi");
    unlock = queue.unlock;
    out = await queue.fetch;
    out = await out.text();
  } finally {
    unlock();
  }

  let console = document.getElementById("console");
  if (out == "\n") return;
  let time = performance.now() / 1000;
  let text = out.split('\n').map(line => `(${time}) pico: ${line}\n`).join("");
  console.innerText += text;
}
function console_log(...args) {
  let console = document.getElementById("console");
  let time = performance.now() / 1000;
  console.innerText += `(${time}) web: ` + args.map(a => a.toString()).join(" ") + "\n";
}

function sleep(timeout) {
  return new Promise((resolve, reject) => {
    setTimeout(resolve, timeout);
  });
}
function asyncTimeout(timeout) {
  return new Promise((resolve, reject) => {
    setTimeout(reject, timeout);
  });
}

// Keep this list in sync with usb_host.h
let usb_status = [
  "DEVICE_UNKNOWN",
  "DEVICE_SELECTED",
  "DEVICE_BOOTSEL_REQUEST",
  "DEVICE_BOOTSEL_COMPLETE",
  "DEVICE_FLASH_DISK_INIT",
  "DEVICE_FLASH_DISK_READ_BUSY",
  "DEVICE_FLASH_DISK_WRITE_BUSY",
  "DEVICE_FLASH_DISK_IO_COMPLETE",
  "DEVICE_FLASH_REQUEST",
  "DEVICE_FLASH_COMPLETE",
  "???", "???", "???", "???", "???", "???",
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
  let unlock;
  try {
    if (!status) {
      status = await queued_fetch("/status.json");
    }

    unlock = status.unlock;
    status = await status.fetch;
    status = await status.json();
  } finally {
    unlock();
  }

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
  let status_timer = undefined;
  if (typeof expected_status === "string") {
    expected_status = usb_status.indexOf(expected_status);
  }
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

      if (expected_status == 0) {
        if (status[device] == expected_status) {
          resolve(status[device]);
          return true;
        }
      } else if (expected_status >= 0x20) {
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
      status_timer = setInterval(update_status, 245, undefined);
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
  } finally {
    if (status_timer) {
      clearInterval(status_timer);
    }
  }
}

let sec = 1000, min = 60 * sec;
let cdc_timeout = 2 * sec;
let msc_timeout = 1 * min;
let flash_timeout = 20 * min;

async function select_device(device, cdc_timeout, msc_timeout) {
  await update_status(await queued_fetch(`/select.cgi?active_device=${device}`));

  if (device >= USB_DEVICES) {
    return;
  }

  if (cdc_timeout == 0 || msc_timeout == 0) {
    return;
  }

  // Informing that the board has selected the device, before awaiting any
  // device responses.
  await wait_for_usb_status(
    device, "DEVICE_SELECTED", 1 * min,
    "Timeout while waiting for the device to be selected");

  // Note, in case of MSC device, the wait would be skipped as the received code
  // is higher than the CDC code.
  await wait_for_usb_status(
    device, "DEVICE_BOOTSEL_REQUEST", cdc_timeout,
    "Timeout while waiting for BOOTSEL request");
  await wait_for_usb_status(
    device, "DEVICE_BOOTSEL_COMPLETE", cdc_timeout,
    "Timeout while waiting for BOOTSEL mode");

  // let wait_for_msc_mounted = wait_for_usb_status(
  //   device, 0x40, timeout, "Timeout while waiting for USB Mass Storage Class");
  // await wait_for_msc_mounted;

  let flash_request = wait_for_usb_status(
    device, "DEVICE_FLASH_REQUEST", msc_timeout,
    "Timeout while waiting for flash request");
  await flash_request;
}

async function clear_status() {
  await select_device(-1, 0, 0);
  await wait_for_usb_status(
    0, "DEVICE_UNKNOWN", 1000,
    "Timeout while waiting for all USB status to be cleared");
}

let range_min = 0;
let range_max = USB_DEVICES;
function set_usb_range(min, max) {
  range_min = min;
  range_max = max;
}

// Return the list of offsets which would have to be patched before sending the
// content to each device.
function locate_uf2_arm_halt(content) {
  // ARM has a HLT instruction which is used for interrupting the program, and
  // which has a 16 bits payload.
  const hlt_op = 0b11010100010;
  const mask = 0b1010101010101010;
  const pattern = (hlt_op << 21) | (mask << 5);

  const hh = (pattern >> 24) & 0xff;
  const hl = (pattern >> 16) & 0xff;
  const lh = (pattern >> 8) & 0xff;
  const ll = pattern & 0xff;

  content = new Uint8Array(content);
  let offsets = [];

  // UF2 file format is divided in chunks of 512 bytes, with 32 bytes of header
  // and 4 bytes of footer. Thus we limit ourself to patch the data within each
  // UF2 chunk.
  for (let off = 0; off + 511 < content.length;) {
    // Skip the UF2 header.
    off += 32;
    // Check and patch the data.
    for (; (off % 512) < 508; off += 4) {
      // NOTE: Little Endian encoding of constants.
      if (content[off][0] != ll ||
          content[off][1] != lh ||
          content[off][2] != hl ||
          content[off][3] != hh) {
        continue;
      }

      offsets.push(off);
    }
    // Skip the footer.
    off += 4;
  }

  return offsets;
}

async function send_uf2_to(device, name, content, offsets, opts) {
  if (opts?.handle_status) {
    stop_status_watchdog();
  }

  // Patch the content with the device index.
  if (offsets.length) {
    let buffer = new Uint8Array(content);
    for (let off in offsets) {
      buffer[off] = device;
      buffer[off + 1] = 0;
      buffer[off + 2] = 0;
      buffer[off + 3] = 0;
    }
  }

  try {
    // Request to switch to the next flashable USB port. the reply from the
    // Pico would tell us whether to send or not the uf2 image again.
    await select_device(device, cdc_timeout, msc_timeout);

    // Make a single request which would be split into multiple by TCP
    // protocol and then throttled by LwIP based on how fast we can forward
    // the content to the USB device.
    console_log(`Flashing content: ${content.byteLength} bytes to flash.`);
    await update_status(await queued_fetch("/flash", {
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

    // Explicitly wait to avoid sending a status request while the memory is
    // filled with the content of the image to be flashed.
    await sleep(1000);

    // Flashing the device takes time, and the previous request only completes
    // once the POST buffer is full, which only implies that everything has
    // been queued, not flashed. Wait 500ms at most before assuming that an
    // unreported error occured.
    let wait_flash_complete = wait_for_usb_status(
      device, "DEVICE_FLASH_COMPLETE", flash_timeout,
      "Timeout while waiting for flash complete");
    await wait_flash_complete;
  } catch(e) {
    console_log(`Unable to flash device at USB port ${device}:\n${e}`);
  } finally {
    if (opts?.unselect) {
      // Unpower all USB devices after having iterated over all of them. Otherwise
      // the last USB port might remain connected.
      select_device(USB_DEVICES, 0, 0);
    }
    if (opts?.handle_status) {
      start_status_watchdog();
    }
  }
}

// content is an array buffer, typed array, blob, json or text.
async function send_uf2(name, content) {
  // When sending uf2 content, we want to avoid making too many request as the
  // flashing pico might already be under pressure.
  stop_status_watchdog();

  // Walk the uf2 content to locate any HALT instruction with a special code to
  // replace it by the index of the device.
  const offsets = locate_uf2_arm_halt(content);

  await clear_status();

  for (let device = range_min; device < range_max; device++) {
    await send_uf2_to(device, name, content, offsets, {});
  }

  // Restart the periodic timer which is asking for status updates.
  start_status_watchdog();
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

function start_status_watchdog() {
  status_timer = setInterval(update_status, 245, undefined);
}
function stop_status_watchdog() {
  clearInterval(status_timer);
}

// Hook the current script and attach it to the DOM.
function setup() {
  // Register an action when new files are selected.
  let dropzone = document.getElementById("dropzone");
  dropzone.addEventListener("drop", dropFilesHandler);
  dropzone.addEventListener("dragover", dropDragOver);

  // Poll the Pico every 500ms to collect new status information about the USB
  // devices. This is useful to unlock promises which are waiting for changes in
  // the state of USB devices.
  start_status_watchdog();
  stdout_timer = setInterval(update_stdout, 1000, undefined);
}

function unsetup() {
  let dropzone = document.getElementById("dropzone");
  dropzone.removeEventListener("drop", dropFilesHandler);
  dropzone.removeEventListener("dragover", dropDragOver);

  stop_status_watchdog();
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
window.set_usb_timeouts = function set_usb_timeouts(cdc, msc, flash) {
  cdc_timeout = cdc|0;
  msc_timeout = msc|0;
  flash_timeout = flash|0;
};
export {
  update_stdout,
  update_status,
  setup,
  unsetup,
  set_usb_range
}
