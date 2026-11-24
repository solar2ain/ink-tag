# Template recipes

- `catalog.json`: template types, labels and recipe links.
- `screen-sizes.json`: supported creation sizes and orientations.
- `layouts.json`: normalized creation coordinates and sample contents.

Recipes only run when creating a page. Saved layouts are fixed-resolution documents;
changing a recipe must not silently reflow an existing user page.

## Small-screen content priority

The root `compact` record sets the maximum long/short edges (300/200 px),
the content inset (4 px) and reserved bottom area (18 px, including the device
mark and separation). An orientation recipe can opt in with
`compact: { "core": <element index>, "fontSize": <fraction of short edge> }`.
Only this core group is created on a matching small screen; its bounds fill the
available area. Other sizes retain the complete composition, including titles.

Course schedules and todo lists opt in. Identity names on business cards are
content, not decorative headings, and are not removed. Native calendar/clock
presets are unaffected. Add geometry and mark-clearance tests whenever another
recipe opts in.

Course tables remain one scene widget. Its `cells` store editable text and local
font/color overrides. Clicks use document-space cell geometry; the temporary DOM
editor is not part of the saved or exported drawing. The preview and opened
canvas both use `lab/leafer/templateLibrary.ts` and the same scene adapter.

## Color and raster clarity

Text/line `color`, table/list `accent`, table `columnColors` and list `itemColors`
are recipe data, not resolution-specific rendering branches. Default templates
use red headings and selective emphasis, keeping long body text black and QR
modules black on white. Existing saved pages keep their chosen colors.

Creation font sizes are whole pixels. Groups containing text render directly
instead of resampling an intermediate bitmap. The viewport snaps its origin to
physical display pixels and uses nearest-neighbor enlargement; this changes no
saved coordinates, image dimensions or firmware renderer. Browser text still
has its original glyph antialiasing: this is not a claim that arbitrary Web
fonts match device WQY or that device color conversion has already happened.
