// import * as jsBinding from 'importmap name';

// Display the output produced by the board and the output produced by this
// script on the console panel displayed on the page.
async function update_stdout() {
  let out = await fetch("/stdout.ssi");
  out = await out.text();
  let console = document.getElementById("console");
  let text = out.split('\n').map(line => `pico: ${line}\n`).join("");
  console.innerText += text;
}
function console_log(...args) {
  console.innerText += "web: " + args.map(a => a.toString()).join(" ") + "\n";
}

function asyncTimeout(timeout, msg) {
  return new Promise((resolve, reject) => {
    setTimeout(reject, timeout, msg);
  });
}

// Keep this list in sync with usb_host.h
let usb_status = [
  "DEVICE_UNKNOWN",
  "DEVICE_BOOTSEL_REQUEST",
  "DEVICE_BOOTSEL_COMPLETE",
  "DEVICE_FLASH_REQUEST",
  "DEVICE_FLASH_DISK_INIT",
  "DEVICE_FLASH_DISK_READ_BUSY",
  "DEVICE_FLASH_DISK_WRITE_BUSY",
  "DEVICE_FLASH_DISK_IO_COMPLETE",
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
let last_status = [];

// Condition which resolves or rejects a promise if the condition is met.
let cond_status = null;

function clear_status() {
  last_status = [];
}

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
    li.innerText = usb_status[s & 0x1f] ?? "???";
    dom.appendChild(li);
  }

  last_status = status;
  if (cond_status && cond_status(status)) {
    cond_status = null;
  }
  return status;
}

async function select_device(device, timeout) {
  let is_selected = new Promise((resolve, reject) => {
    cond_status = function wait_msc_mount(status) {
      // Reject the promise in case of error.
      if (status[device] & 0x10) {
        is_selected.reject(usb_status[status[device] & 0x1f]);
        return true;
      }
      // Wait until the device is mounted as a storage device.
      if (status[device] & 0x40) {
        is_selected.resolve(usb_status[status[device] & 0x1f]);
      }
      return false;
    };
  });
  await update_status(await fetch(`/select.cgi?active_device=${device}`));

  // If the device cannot be selected, we want to fail in order to switch to the
  // next USB device. This timeout is here to make us switch if the USB device
  // is unresponsive.
  let timeout_trigger =
    asyncTimeout(timeout, "Unable to use USB device as a Mass Storage");
  await Promise.race([is_selected, timeout_trigger]);
}

// content is an array buffer, typed array, blob, json or text.
async function send_uf2(name, content) {

  for (let device = 0; device < USB_DEVICES; device++) {
    try {
      // Request to switch to the next flashable USB port. the reply from the
      // Pico would tell us whether to send or not the uf2 image again.
      await select_device(device, 500);

      let is_flashed = new Promise((resolve, reject) => {
        cond_status = function wait_msc_mount(status) {
          // Reject the promise in case of error.
          if (status[device] & 0x10) {
            is_selected.reject(usb_status[status[device] & 0x1f]);
            return true;
          }
          // Wait until the device is flashed completely.
          if ((status[device] & 0x1f) == 8) {
            is_selected.resolve(usb_status[status[device] & 0x1f]);
          }
          return false;
        };
      });

      // Make a single request which would be split into multiple by TCP
      // protocol and then throttled by LwIP based on how fast we can forward
      // the content to the USB device.
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
      let timeout_trigger =
          asyncTimeout(500, "Flashing process incomplete.");
      await Promise.race([is_flashed, timeout_trigger]);
    } catch(e) {
      console_log(`Unable to flash device at USB port ${device}.`);
    }
  }
}

let flash_all_click_handler = null;
async function dropFilesHandler(ev) {
  console_log("File(s) dropped:");
  ev.preventDefault();

  let flashAll = document.getElementById("flash_all");
  if (flash_all_click_handler) {
    flashAll.addEventListener("onclick", flash_all_click_handler);
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
    let header = String.fromCharCode(...new Uint8Buffer(buffer.slice(0, 4)));
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
    for (let file of fileContent) {
      await send_uf2(file.buffer);
    }
    flashAll.disabled = true;
  }

  // Update the button to flash USB devices using the files of the data
  // transfer.
  flash_all_click_handler = clickHandler;
  flashAll.addEventListener("onclick", flash_all_click_handler);
  flashAll.disabled = false;
}

// Hook the current script and attach it to the DOM.
function setup() {
  // Register an action when new files are selected.
  let dropzone = document.getElementById("dropzone");
  dropzone.addEventListener("ondrop", dropFilesHandler);

  // Poll the Pico every 100ms to collect new status information about the USB
  // devices. This is useful to unlock promises which are waiting for changes in
  // the state of USB devices.
  //setInterval(update_status, 100, undefined);
  //setInterval(update_stdout, 250, undefined);
}

setup();

window.update_stdout = update_stdout;
window.update_status = update_status;
export {
  update_stdout,
  update_status
}
