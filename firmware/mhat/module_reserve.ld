/* Copyright (c) 2026 Jonathan Yantis. MIT. */
ASSERT(. - __rom_start_address <= 0x200, "vectors overlap Particle module-info");
. += 0x218 - (. - __rom_start_address);
