# lil web browser thing

## Build & Run

```sh
make               # requires libcurl + SDL2/SDL2_ttf
./browser --help  # (I don't really want to rewrite allat)
```

### GUI

- It's got back, forward, reload, an address bar, "go" and a scroll bar.
- The address bar has in-window URL/searching. 
- History stacks, reloading is a thing (f5/Ctrl+R)
- Image searching with !m flag (in the address bar, and among other flags) or --m from the cli 


Unfortunately I haven't even touched js yet, it's still just html and like barely css


## Search

Typing something that isn't a URL searches. By default that goes to DuckDuckGo's HTML
endpoint, which challenges clients it thinks are scripts (captchas). A browser can't do much
about that, so use a search API instead. Set one of these, either as environment variables
or as `key=value` lines in `~/.browserrc`:

| Setting | What it does |
| --- | --- |
| `BRAVE_API_KEY` / `brave_api_key` | Use the Brave Search API (free tier: https://brave.com/search/api/). Returns JSON, no captchas. |
| `BROWSER_SEARCH_URL` / `search_url` | Any URL template with `%s`, e.g. a SearXNG instance: `https://searx.example.org/search?q=%s&format=json`. JSON responses are rendered as a results page. |

Per query you can force an engine with `!ddg cats` or `!brave cats`.

TLS certificates are verified. For debugging only, `BROWSER_INSECURE_TLS=1` turns that off.

