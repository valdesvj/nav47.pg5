# AI Assistance Policy

## How this project uses AI

This project was **developed with the help of AI** — Claude (Anthropic), Gemini (Google), and Grok (xAI) — as a cooperation. AI assisted with brainstorming, code generation, and documentation, but I reviewed all output, validated results against reference implementations, and take full responsibility for the work and any mistakes.

## Responsible use

- ✅ **I checked the results** — Every engine is checked against its Python twin in the other 47 projects: the almanac (Almanac 47), MOON47, the tides (Tide 47) and the time zones (Zones 47) (`make test`)
- ✅ **I maintained domain expertise** — I guide the AI with architectural decisions based on celestial navigation, tides and passage planning principles, from my work at sea
- ✅ **I take full responsibility** — The work is mine; any errors are mine to fix
- ✅ **I'm transparent** — This file documents how AI was used

## Testing & Validation

- Twin tests: the C engines = the Python reference programs (`make test`: almanac, MOON47 400 cases, tides 478 checks)
- Cross-validated against JPL Horizons and USNO data (as Almanac 47), and NOAA's and TICON's tide predictions
- Every screen checked in the PC simulator (`make sim`, `test/sim/render.py`) before a commit
- Tested on real DM42n hardware

## License

This does not affect the GNU GPL-3.0 license. All code remains open-source and fully accessible.

---

*For more details, see `NOTICE` and the development documentation.*

*Does not replace the Nautical Almanac or official tide tables. Not for use as the primary means of navigation.*
