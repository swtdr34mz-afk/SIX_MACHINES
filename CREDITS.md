# Credits

SIX MACHINES by swtdr34mz. All of its own code (the machines, the macros and sweetspots, the sequencer, the screens)
is written for this project. All sounds are synthesized in the firmware: no samples.

## Built on

| Project | Author | Licence | Used for |
| --- | --- | --- | --- |
| [fm1-base](https://github.com/charlesvestal/fm1-base) | Charles Vestal | MPL-2.0 | the platform: screen, panel, audio, MIDI, storage, simulator, build tools, the Terminus pixel fonts it carries (SIL OFL 1.1) |
| [felucca-bsp](https://github.com/hugelton/felucca-bsp) | Leo Kuroshita (Hügelton Instruments) and contributors | MPL-2.0 | the hardware layer, update loader, packaging |
| JieLi's AC79 SDK | JieLi Technology | Apache-2.0 | three files in every `.fwsc` package (fetched at build time, never copied here) |
