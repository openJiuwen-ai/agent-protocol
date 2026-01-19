import argparse

from mcp.server.fastmcp import FastMCP

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

    group = parser.add_mutually_exclusive_group()
    group.add_argument(
        "--json-response",
        action="store_true",
        default=True,
        help="Return application/json responses (matches C++ isJsonResponseEnabled=true)",
    )
    group.add_argument(
        "--sse",
        action="store_true",
        help="Return text/event-stream responses (matches C++ isJsonResponseEnabled=false)",
    )

    parser.add_argument(
        "--stateless-http",
        action="store_true",
        help="Create a new transport per request (recommended for scalability)",
    )

    return parser.parse_args()


def main() -> None:
    args = _parse_args()

    json_response = args.json_response
    if args.sse:
        json_response = False

    print(f"json_response: {json_response} \n")
    mcp = FastMCP(
        name="cpp-sdk-python-mcp",
        host=args.host,
        port=args.port,
        streamable_http_path=args.path,
        json_response=json_response,
        stateless_http=args.stateless_http,
    )

    @mcp.tool(name="echo")
    def echo(user_query: str = "", message: str = "") -> str:
        return user_query or message

    @mcp.prompt(name="example_prompt", description="Generate a personalized greeting message")
    def example_prompt(name: str, language: str) -> str:
        who = "friend"
        lang = "English"
        if name:
            who = name
        if language:
            lang = language
        return f"Hello, {who}! (language={lang})"

    @mcp.resource(uri="http://example.com/resource", name="Test Resource",
        description="A test resource for demonstration", mime_type="text/plain")
    def resource():
        return f"hello"

    @mcp.resource(uri="http://example.com/resourceTemplate/{id}", name="Test Resource Template",
        description="A test resource template for demonstration", mime_type="text/plain")
    def resource_template(id):
        return f"Hello, World! {id}"

    mcp.run(transport="streamable-http")


if __name__ == "__main__":
    main()
