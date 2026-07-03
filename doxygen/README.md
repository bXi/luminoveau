# API docs pipeline

The website's Docs page renders the engine's real API. Flow:

```
doxygen doxygen/Doxyfile            # GENERATE_XML=YES -> ./xml  (public API only; EXTRACT_PRIVATE=NO)
python3 doxygen/doxy2json.py xml api.json
# upload api.json to the site's docs/api.json  (see .github/workflows/deploy_docs.yml)
```

`doxy2json.py` flattens the many Doxygen XML files into one compact `api.json` the site fetches,
groups classes into sidebar categories (derived from their `src/` sub-directory), and **hides
engine internals**.

## Hiding a class/struct from the website docs

Two ways — prefer the first for one-off internal types, the second for whole backend families.

### 1. Code-side: `@cond` / `@endcond`  (preferred)

Wrap the declaration so Doxygen omits it from the XML entirely — the converter then never sees it,
and nothing needs to change in `doxy2json.py`:

```cpp
/// @cond INTERNAL
struct TaskImpl : TaskBase {   // implementation detail, not part of the public API
    ...
};
/// @endcond
```

(`INTERNAL` isn't in `ENABLED_SECTIONS`, so the block is always excluded. A bare `@cond`/`@endcond`
works too.) This keeps "is this public?" answerable from the header itself.

### 2. Pattern-side: `EXCLUDE_GROUPS` in `doxy2json.py`

For families where annotating every type is impractical (WebGPU/SDL backends, the GPU abstraction,
render passes, bundled libs like `mINI`/`msdfgen`), add a regex to `EXCLUDE_GROUPS`. It matches the
full name and the outermost compound, so nested structs of an excluded class drop with their parent.

## Categories

Sidebar grouping comes from `CATEGORY_RULES` (source-path prefix → category) and the display order
from `CATEGORY_ORDER`, both in `doxy2json.py`. Add a rule to place a new area.
