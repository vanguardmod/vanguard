/*
 * SPDX-License-Identifier: GPL-3.0-or-later
 * SPDX-FileCopyrightText: 2026 wahke <info@wahke.lu> (https://wahke.lu)
 * SPDX-FileCopyrightText: 2026 VanguardMod Project Contributors
 *
 * This file is part of VanguardMod.
 *
 * VanguardMod is built on ETLegacy (https://www.etlegacy.com),
 * which is licensed under GPL-3.0-or-later.
 *
 * VanguardMod is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * VanguardMod is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with VanguardMod. If not, see <https://www.gnu.org/licenses/>.
 */

/*
 * cg_vanguard_dev.h — VanguardMod client-side dev-mode renderer.
 *
 * Public API of the per-frame hitbox visualisation that runs entirely
 * on the client when the server is in dev mode (vanguard_dev=1). See
 * the .c file for the architectural rationale.
 */

#ifndef VANGUARD_CGAME_VANGUARD_DEV_H
#define VANGUARD_CGAME_VANGUARD_DEV_H

/**
 * @brief Per-frame entry. Renders wire boxes for every visible
 *        player when the server has vanguard_dev=1 and the local
 *        client has cg_vanguardDevHitboxes != 0. No-op otherwise.
 *        Call once per CG_DrawActiveFrame, after CG_AddPacketEntities
 *        (so lerpOrigin / lerpAngles are final for this frame).
 */
void CG_VanguardDev_DrawHitboxes(void);

#endif /* VANGUARD_CGAME_VANGUARD_DEV_H */
