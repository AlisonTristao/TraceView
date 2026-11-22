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
| Toggle switch | 96x48 | 150x72 | 260x110 | 1.2-4 | may sit on a widget |
| Slider | 160x44 | 300x72 | 640x100 | 2.5-12 | may sit on a widget |
| LED | 44x44 | 80x104 | 140x170 | 0.5-1.6 | may sit on a widget |
| Line chart | 300x170 | 600x320 | - | 1.5-5 | takes one in its upper left |
| Bar chart | 220x170 | 360x320 | - | 0.8-3 | none |
| Ring gauge | 160x180 | 280x300 | - | 0.7-1.4 | takes one in each bottom corner |
| Half-circle gauge | 200x140 | 320x220 | - | 1.2-2.6 | takes one in each top corner |
| Analog meter | 180x170 | 300x270 | - | 0.95-1.6 | none |
| Bar / LED-bar gauge | 220x100 | 360x190 | - | 1.4-6 | none |
| Digital display | 160x120 | 260x160 | - | 1.2-4 | none |
| Number gauge | 120x90 | 220x160 | - | 0.8-3 | none |
| Scene | 240x200 | 420x340 | - | 0.8-2 | takes one in its top-left corner |
| Text board | 240x160 | 420x300 | - | 0.6-4 | none |
| Serial monitor, robot log, chat | 280x180 | 480x320 | - | 0.6-5 | none |
| Audio analyzer | 300x170 | 560x300 | - | 1.4-4 | none |

A **free zone** is a fraction of the widget's rect where an overlay hides
nothing that matters: the oldest data of a line chart, the corners a round
gauge leaves outside its ring. **Keep clear** areas are the opposite: a line
chart's newest samples on the right and its legend rows, a gauge's value.

## The review (`check_layout`)

`reviewLayout()` takes the widgets in stacking order (later = on top) as
pixel rectangles and reports:

| Kind | Severity | When |
|---|---|---|
| `tooSmall` | problem | Below the kind's minimum. |
| `tooLarge` | warning | 15% past its maximum: the extra room is empty space. |
| `aspect` | warning | Width/height 10% outside the kind's range. |
| `covers` | problem | Two widgets overlap, unless the one on top may overlay and sits inside a free zone of the one below. |
| `alignment` | warning | Two controls in one row whose tops or heights differ by 2-14 px. |
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
