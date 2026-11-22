# Layout guide

How large each widget kind should be, where widgets may stack on each
other, and the review that checks a dashboard against both. The rules live
in [lib/dashboard/layoutadvisor.h](../lib/dashboard/layoutadvisor.h). AI
assistants read them through MCP ([MCP.md](MCP.md)): `list_widget_types`
returns each kind's hints, and `check_layout` reviews the active workspace.

## Layout hints

Sizes are pixels of the whole cell (header included) at normal density.
`list_widget_types` also gives them in cells of the current layout (Large:
60 columns x 40 rows per page).

| Kind | Min | Ideal | Max | Aspect (w/h) | Overlays |
|---|---|---|---|---|---|
| Push button | 72x36 | 132x52 | 240x80 | 1.2-6 | may sit on a widget |
| Toggle switch | 96x64 | 150x72 | 640x110 | 1.2-9 | may sit on a widget |
| Slider | 160x44 | 300x72 | 640x100 | 2.5-12 | may sit on a widget |
| LED | 44x44 | 80x104 | 140x170 | 0.5-1.6 | may sit on a widget |
| Line chart | 300x170 | 600x320 | - | 1.5-5 | takes one in its upper left |
| Bar chart | 220x170 | 360x320 | - | 0.8-3 | none |
| Ring gauge | 160x180 | 280x300 | - | 0.7-1.4 | takes one in each bottom corner |
| Half-circle gauge | 200x140 | 320x220 | - | 1.2-2.6 | takes one in each top corner |
| Analog meter | 180x170 | 300x270 | - | 0.95-1.6 | none |
| Analog dial | 170x190 | 280x300 | - | 0.75-1.35 | none |
| Analog VU meter | 170x140 | 360x220 | - | 1.1-5 | none |
| Analog edgewise meter | 90x200 | 200x300 | - | 0.3-2.5 | none |
| Analog counter | 180x90 | 300x140 | - | 1.5-7 | none |
| Bar / LED-bar gauge | 220x100 | 360x190 | - | 1.4-6 | none |
| Digital display | 160x120 | 260x160 | - | 1.2-4 | none |
| Number gauge | 120x90 | 220x160 | - | 0.8-3 | none |
| Scene | 240x200 | 420x340 | - | 0.8-2 | takes one in its top-left corner |
| Text board | 240x160 | 420x300 | - | 0.6-4 | none |
| Serial monitor, robot log, chat | 280x150 | 480x320 | - | 0.6-5 | none |
| Audio analyzer | 300x170 | 560x300 | - | 1.4-4 | none |

A **free zone** is a fraction of the widget's rect where an overlay hides
nothing that matters: the oldest data of a line chart, the corners a round
gauge leaves outside its ring. **Keep clear** areas are the opposite: a line
chart's newest samples on the right and its legend rows, a gauge's value.

## Spacing

One rule: **the only space between two widgets is the grid's gutter.**
`DashboardGrid` already insets every cell by half a gutter on each side
(`gutter()`: 1% of the canvas' shorter side, 8-32 px, times the density's
`gutterScale`), so two touching widgets are exactly one gutter apart.
Anything inside a widget that adds to it shows as a wider gap.

- **Cards** (charts, gauges, text panels) fill their cell to the edge; their
  own `contentPadding` is inside the card, behind its border.
- **Controls without a plate** have no margin at all
  (`StyledControlWidget::contentMargins()`). In Run mode they're transparent,
  so their content is what the eye measures the gap to.
- **Controls with a plate** (a control style with `screws`; none of the
  built-in ones today) have fixed margins
  inside it, 14 px at the sides and 5 px top and bottom. They are the same in
  edit and Run mode, so nothing moves when you switch.
- **Inside a control**, title, body and state text sit 6 px apart, as one
  block. The block is centered vertically, and left-aligned for the
  switch and slider, so a wider cell never moves them sideways. The
  push button fills its cell up to its maximum and stays centered. The LED
  is a centered column. The switch's state text is as wide as the longer of
  its two texts, so flipping it doesn't move the switch.

So widgets should **touch**: a band of widgets shares one top and bottom,
a column shares one left and right, and the canvas has no holes. The
review below checks all three.

## The review (`check_layout`)

`reviewLayout()` takes the widgets in stacking order (later = on top) as
pixel rectangles and reports:

| Kind | Severity | When |
|---|---|---|
| `tooSmall` | problem | Below the kind's minimum. |
| `tooLarge` | warning | 15% past its maximum: the extra room is empty space. |
| `aspect` | warning | Width/height 10% outside the kind's range. |
| `covers` | problem | Two widgets overlap, unless the one on top may overlay and sits inside a free zone of the one below. |
| `alignment` | warning | Two neighbors of about the same height (a row) whose tops or bottoms differ by 2 px to about a grid row, or of about the same width (a column) whose left or right edges differ by 2 px to about a grid column. Overlays are left out. |
| `emptyArea` | warning | Grid cells no widget covers. The gutter already separates touching widgets, so a gap is unused room; the issue gives the area. |
| `offCanvas` | problem | Past the canvas edge. |

Every issue with a concrete fix carries a suggested rect (in cells, through
MCP). The review also lists the **overlay spots** still free, so an
assistant knows where an LED or a small button can go.

## Laying out a dashboard

1. Bands, top to bottom: the wide charts and round gauges first, then the
   bar-shaped widgets (bar gauges, displays, text panels), then a strip of
   controls.
2. Controls in a strip share one height: 4 rows (72 px) for buttons,
   switches and sliders, 8 rows for LEDs with a title and a state text.
3. A control used with one chart can sit on that chart's free zone instead
   of taking its own cell, such as a Tare button on a line chart, or a
   heartbeat LED, without a title, in a ring gauge's corner. Put it after
   the chart so it stacks on top.
4. Run `check_layout` and fix until it reports no problems.

`tools/chart_preview --snapshot` builds its preset sheets this way and
prints the review of its own layout.
