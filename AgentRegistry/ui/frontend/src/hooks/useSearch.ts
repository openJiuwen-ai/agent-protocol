import { useCallback, useRef, useEffect } from "react";
import type { SearchResponse, SearchMethod, NavigationStep } from "../types";

interface SearchOneOptions {
  onStatus?: (tag: string, msg: string) => void;
  onStep?: (tag: string, step: NavigationStep) => void;
  onResult?: (tag: string, result: SearchResponse) => void;
  onError?: (tag: string, error: string) => void;
}

/**
 * Low-level hook: returns a `searchOne` function that can be called
 * multiple times in parallel for different methods.
 *
 * `tag` is an opaque key passed through to all callbacks so the caller
 * can distinguish e.g. "vector_5" from "vector_10".
 *
 * WebSocket connections are automatically cleaned up on component unmount.
 */
export function useSearch(opts: SearchOneOptions = {}) {
  const wsRefs = useRef<Map<string, WebSocket>>(new Map());

  // Clean up all WebSocket connections on unmount
  useEffect(() => {
    return () => {
      wsRefs.current.forEach((ws) => ws.close());
      wsRefs.current.clear();
    };
  }, []);

  const searchOne = useCallback(
    async (
      query: string,
      method: SearchMethod,
      dataset: string,
      topK: number = 10,
      tag?: string,
    ) => {
      const key = tag ?? method;

      if (method.startsWith("a2x")) {
        return new Promise<void>((resolve) => {
          const protocol = window.location.protocol === "https:" ? "wss:" : "ws:";
          const ws = new WebSocket(`${protocol}//${window.location.host}/api/search/ws`);
          wsRefs.current.set(key, ws);

          ws.onopen = () => {
            ws.send(JSON.stringify({ query, method, dataset, top_k: topK }));
          };

          ws.onmessage = (event) => {
            const msg = JSON.parse(event.data);
            if (msg.type === "status") {
              opts.onStatus?.(key, msg.message);
            } else if (msg.type === "step") {
              opts.onStep?.(key, msg.data as NavigationStep);
            } else if (msg.type === "result") {
              opts.onResult?.(key, msg.data as SearchResponse);
              ws.close();
              wsRefs.current.delete(key);
              resolve();
            } else if (msg.type === "error") {
              opts.onError?.(key, msg.message);
              ws.close();
              wsRefs.current.delete(key);
              resolve();
            }
          };

          ws.onerror = () => {
            opts.onError?.(key, "WebSocket connection failed");
            wsRefs.current.delete(key);
            resolve();
          };
        });
      } else {
        try {
          const resp = await fetch("/api/search", {
            method: "POST",
            headers: { "Content-Type": "application/json" },
            body: JSON.stringify({ query, method, dataset, top_k: topK }),
          });
          if (!resp.ok) throw new Error(`HTTP ${resp.status}`);
          const data: SearchResponse = await resp.json();
          opts.onResult?.(key, data);
        } catch (e: unknown) {
          opts.onError?.(key, e instanceof Error ? e.message : String(e));
        }
      }
    },
    [opts]
  );

  const cancelAll = useCallback(() => {
    wsRefs.current.forEach((ws) => ws.close());
    wsRefs.current.clear();
  }, []);

  return { searchOne, cancelAll };
}
