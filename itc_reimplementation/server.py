from __future__ import annotations

import argparse
import asyncio
import logging

from .protocol import BroadcastController, ProtocolError


async def handle_control(
    reader: asyncio.StreamReader,
    writer: asyncio.StreamWriter,
    controller: BroadcastController,
) -> None:
    authenticated = False
    peer = writer.get_extra_info("peername")
    try:
        while line := await reader.readline():
            try:
                response, authenticated = controller.execute(line.decode("utf-8"), authenticated)
            except UnicodeDecodeError:
                response = "500 invalid encoding"
            except ProtocolError as error:
                response = f"{error.code} {error.message}"
            writer.write(f"{response}\r\n".encode("utf-8"))
            await writer.drain()
            if not authenticated and response == "000 bye":
                break
    finally:
        logging.info("控制连接关闭：%s", peer)
        writer.close()
        await writer.wait_closed()


async def handle_data(reader: asyncio.StreamReader, writer: asyncio.StreamWriter) -> None:
    writer.write(b"505 data channel is not implemented\r\n")
    await writer.drain()
    writer.close()
    await writer.wait_closed()


async def run_server(args: argparse.Namespace) -> None:
    controller = BroadcastController(args.username, args.password)
    control = await asyncio.start_server(
        lambda reader, writer: handle_control(reader, writer, controller), args.host, args.control_port
    )
    data = await asyncio.start_server(handle_data, args.host, args.data_port)
    logging.info("控制服务监听 %s:%d；数据端口 %d 仅保留", args.host, args.control_port, args.data_port)
    async with control, data:
        await asyncio.gather(control.serve_forever(), data.serve_forever())


def main() -> None:
    parser = argparse.ArgumentParser(description="ITC IP 广播控制面复现")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--control-port", type=int, default=8000)
    parser.add_argument("--data-port", type=int, default=15001)
    parser.add_argument("--username", default="admin")
    parser.add_argument("--password", default="admin")
    args = parser.parse_args()
    logging.basicConfig(level=logging.INFO, format="%(message)s")
    try:
        asyncio.run(run_server(args))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
