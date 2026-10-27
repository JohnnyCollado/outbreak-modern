# native/ob_server: licence

SPDX-License-Identifier: AGPL-3.0-or-later

The code in this folder is licensed under the GNU Affero General Public License, version 3 or (at your option) any
later version: https://www.gnu.org/licenses/agpl-3.0.html. The rest of outbreak-modern is GPL-3.0; the two combine
(GPL-3.0 section 13), and this folder keeps its own licence (plan `docs/plans/2026-10-27-ob-server.md`, decision P3).

The full licence text is to be added here as `COPYING.AGPL` before the first release (not fetched in this session).

## Credit

`ob_server` speaks the online protocol of Biohazard Outbreak File #1. What that protocol is was learned by reading
the source of the fan server **Bioserver1** (gh0stl1ne, obsrv.org, AGPL-3.0; packaged at
https://github.com/TAB64-K/ps2-outbreak-server), whose authors reconstructed it from 2014 on. No code was copied; the
behaviour is written down in `research/2026-10-27-lobby-protocol.md` and implemented here from that description.
