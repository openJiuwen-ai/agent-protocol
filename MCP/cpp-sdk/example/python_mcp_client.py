"""
Run from the repository root:
    uv run examples/snippets/clients/streamable_basic.py
"""

import asyncio

import argparse

from mcp import ClientSession
from mcp.client.streamable_http import streamablehttp_client

import logging;
import sys;
logging.basicConfig(level=logging.INFO,
                    format='%(asctime)s - %(name)s - %(levelname)s - %(message)s',
                    handlers=[logging.StreamHandler(sys.stdout)])

def _parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--path", default="/mcp")
    return parser.parse_args()

async def main():
    args = _parse_args()
    endpoint = f"http://{args.host}:{args.port}{args.path}"
    # Connect to a streamable HTTP server
    async with streamablehttp_client(endpoint) as (
        read_stream,
        write_stream,
        _,
    ):
        # Create a session using the client streams
        async with ClientSession(read_stream, write_stream) as session:
            # Initialize the connection
            print(f"start init\n")
            await session.initialize()
            print(f"===============\n")
            print(f"sucess init\n")

            # test tools
            print("start tool test\n")
            tools = await session.list_tools()
            print(f"================\n")
            print(f"Available tools: {[tool.name for tool in tools.tools]}\n")
            result = await session.call_tool("echo", {"user_query": "my test"})
            print(f"==============\n")
            print(result)

            # test prompts
            print("start prompt test\n")
            prompts = await session.list_prompts()
            print(f"===========\n")
            print(prompts)

            prompt = await session.get_prompt(
                    "example_prompt",
                    {
                        "name": "friend",
                        "language": "English",
                    },
                    )
            print(prompt)

            # test resources
            print("start resource test\n")
            resources = await session.list_resources()
            print(resources)
            resource = await session.read_resource("http://example.com/resource")
            print(resource)

            resource_template = await session.list_resource_templates()
            print(resource_template)

        print(f"======================\n")
        print(f"finish client test\n")


if __name__ == "__main__":
    asyncio.run(main())
