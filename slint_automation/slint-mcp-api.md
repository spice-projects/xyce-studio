# Slint Embedded MCP Server — API Reference

Observed against the Slint version used by this project:

* Slint pinned via FetchContent: `v1.18.1` (tag `372cf0ee5577c3dfec309a45e7b778ba4e81b734`, 2026-09-21)
* Server source: `internal/backends/testing/mcp_server.rs`, message schema: `internal/backends/testing/slint_systest.proto`
* **All behavior below was verified live** against a running debug build of this application
  (`SLINT_MCP_PORT=<port> ./.build-debug/xyce-studio`) by direct HTTP probing.

---

## 1. Transport

* Plain HTTP/1.1 server, bound to `127.0.0.1:<SLINT_MCP_PORT>` only (never a public interface).
* Startup: the server starts when `SLINT_MCP_PORT` is set to a valid port number at process
  launch. It logs `Slint MCP server listening on http://127.0.0.1:<port>/mcp` to **stderr** —
  this line is a reliable readiness signal.
* Single endpoint: `POST /mcp` (also accepts `POST /`). Everything else → `404`.
* `OPTIONS` → `204` with CORS headers. Origin header, if present, must be a localhost origin
  (`localhost`, `127.0.0.1`, `::1`); otherwise `403 Forbidden`. Non-browser clients (no Origin
  header) are allowed and get `Access-Control-Allow-Origin: *`.
* `Content-Type` must be `application/json`; otherwise `415`.
* Body max 4 MiB; UTF-8 required; invalid UTF-8 → `400`.
* **No sessions**: no `Mcp-Session-Id`, no `GET`/SSE stream. Pure request/response.
* Keep-alive is supported (server loops reading requests on one connection). `Connection: close`
  is honored. Responses always include `Content-Length`.
* Notifications (requests without `id`) → HTTP `202` with empty body.

## 2. Protocol (JSON-RPC 2.0)

MCP protocol version: `2025-06-18`. Server info: `slint-mcp-embedded` `0.1.0`. Capabilities: `tools`.

### initialize

```json
{"jsonrpc":"2.0","id":1,"method":"initialize","params":{"protocolVersion":"2025-06-18","capabilities":{},"clientInfo":{"name":"probe","version":"0.0.1"}}}
```

Response `result`:

```json
{
  "protocolVersion": "2025-06-18",
  "capabilities": {"tools": {}},
  "serverInfo": {"name": "slint-mcp-embedded", "version": "0.1.0"},
  "instructions": "... (long usage guide, includes enum value lists)"
}
```

The `instructions` field contains a workflow guide and the authoritative enum value lists —
a good source for feature detection at connect time.

### notifications/initialized

```json
{"jsonrpc":"2.0","method":"notifications/initialized"}
```

→ HTTP `202`, empty body. Optional; the server treats unknown notifications as no-ops.

### Supported methods

| Method | Behavior |
| --- | --- |
| `initialize` | returns server info (above) |
| `notifications/initialized` | notification, no response |
| `tools/list` | returns 14 tool definitions |
| `tools/call` | invokes a tool |
| anything else | JSON-RPC error `-32601` "Method not found" |

**Batch requests are NOT supported** → error `-32600`.
Malformed JSON → error `-32700` "Parse error".

### tools/call response envelope

Successful JSON tool result:

```json
{"jsonrpc":"2.0","id":3,"result":{"content":[{"type":"text","text":"<pretty-printed JSON>"}]}}
```

**Important**: the `text` field of a JSON tool result is a *pretty-printed JSON document as a
string* — the client must `json.loads()` it a second time to get the data.

Screenshot result: `content` is an `image` block (base64 PNG + `mimeType: "image/png"`)
followed by a `text` block with metadata, e.g. `{"sizeBytes": 63684}`.

Tool-level failure (`isError: true`, HTTP 200 — not a JSON-RPC error):

```json
{"jsonrpc":"2.0","id":10,"result":{"content":[{"text":"Error: Unknown tool: bogus_tool","type":"text"}],"isError":true}}
```

Protocol-level failure (JSON-RPC error object, not `result`):

```json
{"jsonrpc":"2.0","id":11,"error":{"code":-32601,"message":"Method not found: resources/list"}}
```

## 3. Handles

All handles are JSON objects with **string-valued** integer fields (protobuf JSON convention):

```json
{"index": "41", "generation": "1"}
```

Verified live behaviors:

* **Window handles and element handles are separate kinds** with identical shape. Window
  handles come from `list_windows`; element handles from `get_element_tree`,
  `find_elements_by_id`, `query_element_descendants`, or `get_window_properties` →
  `rootElementHandle`. They are not interchangeable.
* **Handles are ephemeral, not stable across calls.** Verified: the same element appeared as
  handle `{"index":"3"}` in one `get_element_tree` call and `{"index":"31"}` in a later
  `find_elements_by_id` call. Handle indexes are assigned from a growing arena per query.
  The framework MUST re-resolve a locator immediately before every action/property read and
  never cache handles between calls.
* Zero-valued fields may be omitted when *sending* (`{}` ≡ `{"index":"0","generation":"0"}`),
  but a real window returned `{"index":"1","generation":"1"}`, so a default `{}` handle is
  normally invalid ("Error: Invalid handle").
* Stale generation (e.g. element destroyed / window recreated) → tool error
  `Error: Invalid handle`.

## 4. Tools

All 14 tools verified live. Arguments below use the exact schema field names.

### Discovery

#### list_windows — no arguments

```json
{"windowHandles": [{"generation": "1", "index": "1"}]}
```

#### get_window_properties — `{windowHandle}`

```json
{
  "position": {"x": 1446, "y": 280},
  "rootElementHandle": {"generation": "1", "index": "1"},
  "scaleFactor": 2.0,
  "size": {"height": 1360, "width": 1800}
}
```

`rootElementHandle` is the entry point for tree traversal. Size is **physical pixels**;
`scaleFactor` converts to logical (e.g. ÷2.0 on Retina).

#### get_element_tree — `{elementHandle, maxElements?}`

Flat breadth-order list of the subtree. `maxElements` default 200, clamped to [1, 1000].

```json
{
  "elements": [
    {
      "handle": {"generation": "1", "index": "1"},
      "typeNamesAndIds": [{"typeName": "Window", "id": "MainWindow::root"}],
      "accessibleRole": "None",
      "accessibleLabel": "",
      "size": {"width": 900.0, "height": 28.0},
      "absolutePosition": {"x": 0.0, "y": 0.0},
      "computedOpacity": 1.0,
      "layoutKind": "NotALayout"
    }
  ],
  "totalCount": 30,
  "truncated": true
}
```

`truncated: true` means use targeted queries instead of a bigger `maxElements`.

#### find_elements_by_id — `{windowHandle, elementsId}`

Matches Slint **element IDs** as declared in `.slint` source (`ToolbarButton::ta := TouchArea
{ ... }`), i.e. the `match_id` query — **not** the `accessible-id` property. IDs are
**qualified**: `"ComponentName::element-id"`.

```json
{"elementHandles": [{"generation": "1", "index": "31"}]}
```

Verified live behaviors:

* **No match returns `{}`, not `{"elementHandles": []}`** — the protobuf JSON
  serializer omits empty repeated fields, so clients must default to an empty
  list when the key is absent.
* The window root element's listed id (`MainWindow::root`) is NOT matchable
  through this tool; nested declared ids (`MainWindow::toolbar`,
  `ToolbarButton::ta`) match fine.

Returns an empty list when nothing matches (no error).

#### query_element_descendants — `{elementHandle, queryStack, findAll?}`

Pipeline search. Each `queryStack` entry has exactly one of:
`matchDescendants` (bool, recurse), `matchElementId`, `matchElementTypeName`,
`matchElementTypeNameOrBase`, `matchElementAccessibleRole` (PascalCase role).
Instructions apply in order. Returns `{"elementHandles": [...]}`.
More efficient than `get_element_tree` for targeted lookups.

**Verified live caveat**: `findAll: true` visits and returns **duplicated
elements** — the traversal reports 338 visited nodes for a 73-element tree,
with matches repeated once per nesting path (e.g. the single status bar
`Text` is returned 4 times). Use `findAll: false` (first-match semantics) for
single-element lookup; the framework's `find_by_role` does exactly that.

#### get_element_properties — `{elementHandle}`

```json
{
  "typeNamesAndIds": [{"typeName": "Rectangle", "id": "MainWindow::toolbar"}],
  "absolutePosition": {},
  "computedOpacity": 1.0,
  "size": {"height": 57.0, "width": 900.0},
  "layoutKind": "NotALayout"
}
```

Also includes (when present): `accessibleRole`, `accessibleLabel`, `accessibleValue`,
`accessibleValueMinimum/Maximum/Step`, `accessibleDescription`, `accessibleChecked`,
`accessibleCheckable`, `accessibleEnabled`, `accessibleReadOnly`,
`accessiblePlaceholderText`. Zero-valued numeric/enum fields are omitted (protobuf JSON).

### Interaction

#### click_element — `{elementHandle, action?, button?}`

Simulated mouse click at element center. Defaults: `SingleClick`, `Left`. Empty-object
response `{}`. Verified live: produces press → move → release events, all `Accepted`.

#### drag_element — `{elementHandle, target, button?}`

Drag from element center to `{"x": <logical>, "y": <logical>}` in interpolated steps.

#### scroll_element — `{elementHandle, deltaX?, deltaY?}`

Send a mouse wheel event over the element center. `deltaX`/`deltaY` are logical pixels;
a **negative `deltaY` reveals content further down** (wheel-down; verified against the
server unit test `test_scroll_element_scrolls_a_flickable`, which asserts a Flickable
with `content-height: 400` ends at `content-y == -50` after `deltaY: -50`). Use this to
scroll a `ScrollView`/`Flickable` whose area the element center falls into, and for
wheel-driven gestures such as canvas zoom. Newer servers also expose `hover_element`,
`move_pointer` and `dispatch_pointer_scroll` (window-level wheel at a position, arguments
`{windowHandle, position, deltaX?, deltaY?}`).

#### dispatch_pointer_scroll — `{windowHandle, position, deltaX?, deltaY?}`

Send a mouse wheel event at a logical window position; `use scroll_element` to scroll by
element handle.

#### invoke_accessibility_action — `{elementHandle, action}`

`Default_` (activate button/checkbox), `Increment`/`Decrement` (slider, spinbox),
`Expand` (combo box). Preferred over clicks when the role suggests a semantic action.

#### set_element_value — `{elementHandle, value}`

Sets accessible value. For text inputs this sets the text content (the framework's `fill()`);
for sliders pass the number as a string.

#### dispatch_key_event — `{windowHandle, text, eventType?}`

`PressAndRelease` (default) for typing, `Press`/`Release` for modifiers. Note: takes the
**window** handle, sends to the focused element.

`text` must be the Slint `KeyEvent` text: the literal character for character keys, or
the control code behind the `Key.*` constants — `Key.Tab` = `"\t"`, `Key.BackTab` =
`"\u0019"`, `Key.Escape` = `"\u001b"`, `Key.Return` = `"\n"`, `Key.Space` = `" "`
(see Slint `internal/common/key_codes.rs`). Key *names* like `"Tab"` or `"Escape"` are
accepted without error but match nothing: any `key-pressed` handler comparing
`event.text == Key.Tab` never sees them. Verified live: `"\t"` moves tab focus through
dialog input controls (when the modal `FocusScope` rejects Tab), `"\x1b"` triggers
`dismissed()`, while literal `"Tab"`/`"Escape"` are swallowed silently.

#### take_screenshot — `{windowHandle}`

Returns MCP `image` content block (base64 PNG, physical pixel size, RGBA) + metadata text
block. Verified live: 1800×1360 PNG on a 2× display with 900×680 logical window.

#### start_event_recording / stop_event_recording — no arguments

`stop` returns:

```json
{
  "events": [
    {"sequence": 1, "timestampMs": 0, "windowHandle": {...}, "event": {"pointerPressed": {...}}, "result": "Accepted"}
  ],
  "droppedCount": 0,
  "unknownEventCount": 0
}
```

Verified live: a click produced `pointerMoved`, `pointerPressed`, `pointerMoved`,
`pointerReleased`, `windowActiveChanged` — all `Accepted`. Use to assert that input actually
reached Slint. `droppedCount` is non-zero when the 1024-entry buffer overflowed.

## 5. Error behavior summary

| Condition | Result |
| --- | --- |
| Unknown tool name | `tools/call` succeeds, `isError: true`, `Error: Unknown tool: <name>` |
| Missing required argument | `isError: true`, `Error: missing windowHandle` |
| Bad enum value | `isError: true`, `Error: invalid button value: <n>` |
| Invalid/stale handle | `isError: true`, `Error: Invalid handle` |
| Unknown method | JSON-RPC error `-32601` |
| Batch request | JSON-RPC error `-32600` |
| Malformed JSON | JSON-RPC error `-32700` |
| Wrong content type | HTTP `415` |
| Bad Origin | HTTP `403` |
| Unknown path | HTTP `404` |

Note: tool failures are **never** JSON-RPC errors — the framework must check `isError` on
every `tools/call` result.

## 6. Implications for the test framework (design deltas)

1. **`get_by_id` maps to Slint element IDs** (`MainWindow::toolbar`), not `accessible-id`
   (§10/§31 of design.md must be corrected). IDs must be qualified `ComponentName::element-id`;
   the root window id is `MainWindow::root`.
2. **Never cache handles** (§12). Re-resolve immediately before each action/read; treat
   `Error: Invalid handle` as "re-resolve and retry once" (matches §20.2).
3. Double JSON decode: MCP `text` blocks wrap pretty-printed JSON.
4. Every `tools/call` must check `isError` and raise `McpError` with the text payload.
5. Readiness detection: wait for the stderr line `Slint MCP server listening on` **and/or**
   poll `POST /mcp` with `initialize` (the only reliable protocol-level check).
6. `fill()` = `set_element_value` on the TextInput element; there is no character-level
   `type()` unless done via repeated `dispatch_key_event`.
7. Text content of elements: `accessibleLabel` / `accessibleValue` in
   `get_element_properties`; `to_have_text` asserts against these.
8. Visibility/enabled: `computedOpacity`, `accessibleEnabled`; `to_be_visible` /
   `to_be_enabled` map to these properties (an explicit `visible` property is not exposed).
9. `expect(...).to_exist()` maps to `find_elements_by_id` returning a non-empty list.
10. Screenshots are whole-window only (no per-element crops); element crops would be done
    client-side using `absolutePosition`/`size` (logical) × `scaleFactor` (physical).

## 7. Probed examples

Raw request/response pairs captured during live verification are kept in `/tmp/opencode`
(`wins.json`, `winprops.json`, `tree.json`, `find.json`, `props.json`, `r20.json`–`r23.json`,
`shot.json`) — regenerate with the app running:

```bash
SLINT_MCP_PORT=37421 ./.build-debug/xyce-studio &
curl -s -X POST http://127.0.0.1:37421/mcp -H 'Content-Type: application/json' \
  -d '{"jsonrpc":"2.0","id":1,"method":"initialize","params":{}}'
```
