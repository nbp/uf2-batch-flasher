#!/usr/bin/env nix-shell
#! nix-shell -i python -p python3

import asyncio
import argparse
import os
import time
from enum import Enum

USB_DEVICES = 64
usb_status = [
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
]

sec = 1  # asyncio.timeout has a delay expressed in seconds.
minute = 60 * sec
cdc_timeout = 2 * sec
msc_timeout = 1 * minute
flash_timeout = 20 * minute

# The MSS of TCP is set to 1460, and we need extra bytes to identify this TCP
# request as some content to be flashed.
flash_window = 1460 - 4

def verbose(s):
    # print(f"verbose: {s}")
    pass

def debug(s):
    # print(f"debug: {s}")
    pass

# Implement a way to wait for a specific event coming from the server.
class AwaitQueue:
    name = ""
    lock = None
    await_queue = None
    result_queue = None
    value_count = 0

    def __init__(self, name):
        self.name = name
        self.await_queue = []
        self.result_queue = []

    def prefetch(self):
        turn = asyncio.Future()
        if self.result_queue != []:
            vid, value = self.result_queue[0]
            self.result_queue = self.result_queue[:1]
            turn.set_result(value)
            return turn

        vid = self.value_count
        self.value_count += 1
        self.await_queue.append((vid, turn))
        return turn

    async def fetch(self):
        if self.result_queue != []:
            vid, value = self.result_queue[0]
            verbose(f"AwaitQueue {self.name}:    {vid}-->: Deque {value}")
            self.result_queue = self.result_queue[:1]
            return value

        vid = self.value_count
        self.value_count += 1
        verbose(f"AwaitQueue {self.name}:  ? {vid}   : Wait")
        turn = asyncio.Future()
        self.await_queue.append((vid, turn))
        value = await turn
        verbose(f"AwaitQueue {self.name}:    {vid}-->: Resume {value}")
        return value

    async def fetch_new(self):
        self.clear_outdated()
        return await self.fetch()

    def clear_outdated(self):
        verbose(f"AwaitQueue {self.name}: Clear")
        self.result_queue = []

    def clear_exceptions(self):
        # When a timeout occurs during the `await` from fetch, the associated
        # future is called with set_exception, which flags the Future as done.
        while self.await_queue != []:
            vid, turn = self.await_queue[0]
            if not turn.done():
                return
            verbose(f"AwaitQueue {self.name}:    {vid} X : Exception")
            self.await_queue = self.await_queue[1:]

    def received(self, value):
        self.clear_exceptions()

        if self.await_queue != []:
            vid, turn = self.await_queue[0]
            verbose(f"AwaitQueue {self.name}: -->{vid}   : Resolve")
            self.await_queue = self.await_queue[1:]
            turn.set_result(value)
            return

        vid = self.value_count
        self.value_count += 1
        verbose(f"AwaitQueue {self.name}: -->{vid}   : Queue")
        self.result_queue.append((vid, value))

# Equivalent of recv_msg_t enum
class ClientMsg(Enum):
    REQUEST_STATUS = 0x00
    REQUEST_STDOUT = 0x01
    SELECT_DEVICE = 0x02
    START_FLASH = 0x03
    WRITE_FLASH_PART = 0x04
    END_FLASH = 0x05
    REBOOT_FOR_FLASH = 0x06
    REBOOT_SOFT = 0x07

# Equivalent of send_msg_t enum
class ServerMsg(Enum):
    UPDATE_STATUS = 0x80
    UPDATE_STDOUT = 0x81
    FLASH_START = 0x82
    FLASH_PART_RECEIVED = 0x83
    FLASH_PART_WRITTEN = 0x84
    FLASH_END = 0x85
    FLASH_ERROR = 0x86
    DECODE_FAILURE = 0x87

async def tcp_send(tcp, data):
    tcp.writer.write(bytes(data))
    await tcp.writer.drain()

async def send_request_status(tcp):
    await tcp_send(tcp, [ClientMsg.REQUEST_STATUS.value])

async def send_request_stdout(tcp):
    await tcp_send(tcp, [ClientMsg.REQUEST_STDOUT.value])

async def send_reboot_for_flash(tcp):
    await tcp_send(tcp, [ClientMsg.REBOOT_FOR_FLASH.value])

async def send_reboot_soft(tcp):
    await tcp_send(tcp, [ClientMsg.REBOOT_SOFT.value])

async def send_select_device(tcp, device):
    await tcp_send(tcp, [ClientMsg.SELECT_DEVICE.value, device & 0xff])

async def send_start_flash(tcp):
    await tcp_send(tcp, [ClientMsg.START_FLASH.value])

async def send_write_flash_part(tcp, part):
    length = len(part)
    msg = [
        ClientMsg.WRITE_FLASH_PART.value,
        length & 0xff,
        length >> 8
    ] + list(part)
    await tcp_send(tcp, msg)

async def send_end_flash(tcp):
    await tcp_send(tcp, [ClientMsg.END_FLASH.value])

update_status_msg = AwaitQueue("update_status")
def recv_update_status(data):
    devices = data[1] + (data[2] << 8)
    status = data[3:3 + devices]
    update_status_msg.received(status)
    return devices + 3

update_stdout_msg = AwaitQueue("update_stdout")
def recv_update_stdout(data):
    length = data[1] + (data[2] << 8)
    msg = data[3:3 + length]
    update_stdout_msg.received(msg)
    return length + 3

flash_start_msg = AwaitQueue("flash_start")
def recv_flash_start(data):
    flash_start_msg.received(None)
    return 1

flash_part_received_msg = AwaitQueue("flash_part_received")
def recv_flash_part_received(data):
    flash_part_received_msg.received(None)
    return 1

flash_part_written_msg = AwaitQueue("flash_part_written")
def recv_flash_part_written(data):
    flash_part_written_msg.received(None)
    return 1

flash_end_msg = AwaitQueue("flash_end")
def recv_flash_end(data):
    flash_end_msg.received(None)
    return 1


def tcp_recv(tcp, data):
    msg_id = data[0]
    if msg_id == ServerMsg.UPDATE_STATUS.value:
        return recv_update_status(data)
    elif msg_id == ServerMsg.UPDATE_STDOUT.value:
        return recv_update_stdout(data)
    elif msg_id == ServerMsg.FLASH_START.value:
        return recv_flash_start(data)
    elif msg_id == ServerMsg.FLASH_PART_RECEIVED.value:
        return recv_flash_part_received(data)
    elif msg_id == ServerMsg.FLASH_PART_WRITTEN.value:
        return recv_flash_part_written(data)
    elif msg_id == ServerMsg.FLASH_END.value:
        return recv_flash_end(data)
    elif msg_id == ServerMsg.FLASH_ERROR.value:
        print("tcp: pico: An error occured while flashing the device.\n")
    elif msg_id == ServerMsg.DECODE_FAILURE.value:
        print("tcp: pico: Unexpected message id\n")
    else:
        raise Exception(f"Unexpect message: {data.hex()}")
    return 1


async def tcp_fetch(tcp):
    # while not tcp.writer.is_closing():
    while True:
        # Receive the response from the server
        data = await tcp.reader.read(n = 2048)
        if len(data) == 0:
            await asyncio.sleep(0.1)
            continue
        debug(f"client: Received message: {data.hex()}\n")
        recv = 0
        while recv < len(data):
            try:
                length = tcp_recv(tcp, data[recv:])
                debug(f"client: Processed message {data[recv:recv+length].hex()}")
                verbose(f"client: {len(data) - recv - length} bytes remain.")
                recv += length
            except Exception as e:
                print(f"Error while processing buffer:   {data.hex()}")
                print(f"Error: {e}")
                raise
    print("End of server stream")


# Function to fetch stdout and display it
async def update_stdout(tcp, flush):
    text = b""
    while True:
        prefetch = update_stdout_msg.prefetch()
        if not prefetch.done():
            await send_request_stdout(tcp)
        text += await prefetch

        lines = text.split(b'\n')
        # If the text ends with a new line, then text would be empty, otherwise
        # it would be non-empty and we would keep cycling without sleeping to
        # fetch the rest of the stdout buffer.
        text = lines[-1]
        lines = lines[:-1]
        # We cannot decode utf-8 sooner as it might contain symbols which span
        # multiple bytes.
        if lines != []:
            print('\n'.join([f"pico: {line.decode('utf-8')}" for line in lines]))
        if text == b"":
            if flush:
                flush.set_result(True)
                flush = None
            await asyncio.sleep(0.5)


def check_for_expectation(status, expected_status):
    # Raise an exception in case of error.
    if status & 0x10:
        raise Exception(f"Unexpected status code: 0x{status:x}")

    if expected_status == 0:
        # Wait until it settles on a given status.
        return status == expected_status
    elif expected_status >= 0x20:
        # Wait until the device is mounted.
        return status & expected_status == expected_status
    else:
        # Wait until a stage is reached or settled beyond.
        return (status & 0x0f) >= expected_status
    return False


def status_name(code):
    text = usb_status[code & 0x1f]
    if code & 0x10:
        text += " (error)"
    if code & 0x20:
        text += " (tuh mounted)"
    if code & 0x40:
        text += " (msc mounted)"
    if code & 0x40:
        text += " (cdc mounted)"
    return text

all_status = []


async def wait_for_usb_status(tcp, device, expected_status, timeout, msg):
    print(f"USB {device}: Waiting for status {expected_status}")
    global all_status
    expected_status = usb_status.index(expected_status)
    last_status = "STATUS_UNKNOWN"
    current_status = "STATUS_UNKNOWN"

    # Attempt a first time from the last status, but being forgiving in case of
    # bad error codes.
    try:
        last_status = current_status
        current_status = all_status[device]
        if last_status != current_status:
            print(f"USB {device}: {status_name(current_status)}")
        if check_for_expectation(current_status, expected_status):
            return True
    except Exception:
        pass

    # Retry with updated status values.
    try:
        update_status_msg.clear_outdated()
        async with asyncio.timeout(timeout):
            while True:
                prefetch = update_status_msg.prefetch()
                if not prefetch.done():
                    await send_request_status(tcp)
                all_status = await prefetch

                last_status = current_status
                current_status = all_status[device]
                if last_status != current_status:
                    print(f"USB {device}: {status_name(current_status)}")
                if check_for_expectation(current_status, expected_status):
                    return True
                await asyncio.sleep(0.2)
    except TimeoutError:
        raise Exception(msg)


async def select_device(tcp, device, cdc_timeout = 0, msc_timeout = 0):
    print(f"Select device {device}")
    await send_select_device(tcp, device)
    if device >= USB_DEVICES:
        return
    if cdc_timeout == 0 or msc_timeout == 0:
        return

    await wait_for_usb_status(tcp, device, "DEVICE_SELECTED", 1 * minute,
                              "Timeout while waiting for device selection")
    await wait_for_usb_status(tcp, device, "DEVICE_BOOTSEL_REQUEST", cdc_timeout,
                              "Timeout while waiting for BOOTSEL request")
    await wait_for_usb_status(tcp, device, "DEVICE_BOOTSEL_COMPLETE", cdc_timeout,
                              "Timeout while waiting for BOOTSEL mode")
    await wait_for_usb_status(tcp, device, "DEVICE_FLASH_REQUEST", msc_timeout,
                              "Timeout while waiting for flash request")


async def clear_status(tcp):
    await send_select_device(tcp, -1)
    await wait_for_usb_status(tcp, 0, "DEVICE_UNKNOWN", 1 * minute, "Timeout while clearing USB status")


def locate_uf2_arm_halt(content):
    # ARM HLT instruction with a 16-bit payload.
    hlt_op = 0b11010100010
    mask = 0b1010101010101010
    pattern = (hlt_op << 21) | (mask << 5)

    # Split the pattern into little-endian format (byte order)
    ll = pattern & 0xff
    lh = (pattern >> 8) & 0xff
    hl = (pattern >> 16) & 0xff
    hh = (pattern >> 24) & 0xff

    # Initialize a list to store the offsets
    offsets = []

    # UF2 file format: chunks of 512 bytes with 32 bytes of header and 4 bytes of footer.
    chunk_size = 512
    header_size = 32
    footer_size = 4

    # Iterate through the content by skipping headers and footers of UF2 chunks
    off = 0
    while off + chunk_size - 1 < len(content):
        # Skip the UF2 header.
        off += header_size

        # Scan through the data section of the UF2 chunk (skip footer too)
        while (off % chunk_size) < (chunk_size - footer_size):
            # Check if the 4 bytes match the little-endian encoded pattern
            if content[off] == ll and content[off + 1] == lh and \
               content[off + 2] == hl and content[off + 3] == hh:
                # Store the offset where the pattern is found
                offsets.append(off)

            # Move to the next 4-byte sequence
            off += 4

        # Skip the UF2 footer.
        off += footer_size

    return offsets


async def send_uf2_to(tcp, name, device, content, offsets):
    # Patch the content with the device index.
    for off in offsets:
        print(f"Patching offset {off} with device id {device}.")
        content[off] = device
        content[off+1] = 0
        content[off+2] = 0
        content[off+3] = 0
    try:
        # Switch to the device that we are going to flash.
        await select_device(tcp, device, cdc_timeout, msc_timeout)

        print(f"Flashing content: {len(content)} bytes to flash.")
    
        prefetch = flash_start_msg.prefetch()
        await send_start_flash(tcp)
        await prefetch

        sent = 0
        flash_part_received_msg.clear_outdated()
        flash_part_written_msg.clear_outdated()
        
        # If we were to send all data at once, the UF2 Batch Flasher might run
        # out of memory. Thus we send it in a limited number of chunks, and wait
        # until the next chunk is available to send more data. At the beginning
        # they are all available.
        sent_queue = [asyncio.Future() for _ in range(16)]
        for f in sent_queue:
            f.set_result(True)
        
        # Send each chunk without overflowing the server.
        last_time = time.perf_counter() * 1000
        while sent < len(content):
            # Wait until the server queue has an empty slot, and pre-allocate
            # the reception of the chunk that we are about to send.
            await sent_queue[0]
            sent_queue = sent_queue[1:]
            sent_queue.append(flash_part_written_msg.prefetch())

            # Send the next chunk.
            now = time.perf_counter() * 1000
            print(f"(waited {now - last_time:.0f}ms) Sending bytes[{sent}:{sent + flash_window}]")
            prefetch = flash_part_received_msg.prefetch()
            await send_write_flash_part(tcp, content[sent: sent + flash_window])
            await prefetch

            # Wait until the last chunk is received. (optional)
            sent = sent + flash_window
            last_time = now
        
        # Wait until all have been written.
        while sent_queue != []:
            await sent_queue[0]
            sent_queue = sent_queue[1:]
        
        # Close the file.
        prefetch = flash_end_msg.prefetch()
        await send_end_flash(tcp)
        await prefetch
        
        await wait_for_usb_status(tcp, device, "DEVICE_FLASH_COMPLETE", flash_timeout,
                                  "Timeout while waiting for flash completion")
    except Exception as e:
        print(f"Unable to flash device at USB port {device}:\n{e}")


async def send_uf2(tcp, name, content, args):
    # Walk the uf2 content to locate any HALT instruction with a special code to
    # replace it by the index of the device.
    offsets = locate_uf2_arm_halt(content)
    await clear_status(tcp)

    devices = []
    if args.single:
        devices = [args.single]
    elif args.start_with and args.end_with:
        devices = range(args.start_with, args.end_with + 1)
    elif args.start_with and not args.end_with:
        devices = range(args.start_with, USB_DEVICES)
    elif not args.start_with and args.end_with:
        devices = range(args.end_with + 1)
    else:
        devices = range(USB_DEVICES)

    for device in devices:
        await send_uf2_to(tcp, name, device, content, offsets)
        # await asyncio.sleep(1)

    await select_device(tcp, USB_DEVICES)


class TCPConn:
    reader = None
    writer = None


async def main(args):
    print("Connecting to the UF2 Batch Flasher")
    reader, writer = await asyncio.open_connection(args.host, args.port)
    tcp = TCPConn()
    tcp.reader = reader
    tcp.writer = writer
    print("Connected...")

    flush_stdout = asyncio.Future()

    # Listen for messages from the server.
    tcp_receiver = asyncio.create_task(tcp_fetch(tcp))

    # Monitor the output from the UF2 Batch Flasher.
    stdout_fwd = asyncio.create_task(update_stdout(tcp, flush_stdout))

    # Wait until we pull all stdout content from the board.
    await flush_stdout

    # Read the file and send the content to every UF2 device.
    file_path = args.uf2_file
    with open(file_path, "rb") as f:
        content = bytearray(f.read())
        await send_uf2(tcp, os.path.basename(file_path), content, args)

    if args.reboot:
        print("Send soft-reboot command")
        send_reboot_soft(tcp)

    print("Closing the connection")
    writer.close()
    await writer.wait_closed()

    tcp_receiver.cancel()
    try:
        await tcp_receiver
    except asyncio.CancelledError:
        pass

    stdout_fwd.cancel()
    try:
        await stdout_fwd
    except asyncio.CancelledError:
        pass

if __name__ == '__main__':
    parser = argparse.ArgumentParser(
        description="Flash a UF2 file to a USB device.")
    parser.add_argument('--single', type=int,
                        help='Flash a single device')
    parser.add_argument('--start-with', type=int,
                        help='Specify the first device to flash')
    parser.add_argument('--end-with', type=int,
                        help='Specify the last device to flash')
    parser.add_argument('--host', type=str, default="192.168.1.52",
                        help='Host of the UF2 Batch Flasher')
    parser.add_argument('--port', type=int, default=5656,
                        help='Port of the UF2 batch flasher')
    parser.add_argument('--reboot', type=bool, default=False,
                        help='Reboot once the operations are done')
    parser.add_argument('uf2_file', help='Path to the UF2 file to flash')
    args = parser.parse_args()

    asyncio.run(main(args))
