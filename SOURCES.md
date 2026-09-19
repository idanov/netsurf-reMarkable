# Source origins and licenses

The browser and framebuffer library were consolidated as source snapshots into
this repository. They are ordinary directories, maintained together with the
build and deployment tooling; their former repositories are not required to
clone, build, or develop this project. Earlier component history is not imported
into the main repository history.

| Directory | Original repository | Imported revision |
| --- | --- | --- |
| `netsurf/` | https://github.com/idanov/netsurf-base-reMarkable | `4bf203f1c67204d661f7ed17fedc629ac5016de2` |
| `libnsfb/` | https://github.com/idanov/libnsfb-reMarkable | `aeac1767e9816bf03d90afb795521657ec98b253` |

The snapshots came from the nested working copies used by the integration build.
Compared with the sibling clones present at consolidation time:

- The browser includes the history fix after `c1a18e88a`.
- The framebuffer library includes additional landscape refresh and mutex changes
  compared with `f3da239`.

Existing copyright notices and license files are retained. The integration
code's [MIT license](LICENSE) does not replace component licenses:

- Browser: [netsurf/COPYING](netsurf/COPYING), GNU GPL version 2; consult individual
  files for their notices and any additional licensing terms.
- Framebuffer library: [libnsfb/COPYING](libnsfb/COPYING), MIT.

Other third-party dependencies remain external and are fetched by `Dockerfile`.
Their version selections and source locations are recorded there.
