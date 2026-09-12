### NOTE: This fork is solely tested and used with https://github.com/whrvt/McOsu-ng and https://github.com/neomodnet/neomod
### Any other usecase is (currently) not supported.

SoLoud
======

SoLoud is an easy to use, free, portable c/c++ audio engine for games.

![ScreenShot](https://raw.github.com/whrvt/neoloud/master/soloud.png)

Zlib/LibPng licensed. Portable. Easy.

This fork adds per-voice time-stretching and pitch-shifting (`setTempo`, `setPitchShift`) on top of the stock engine: a tempo above 1.0 with no pitch shift is stretched in the time domain by the engine's own WSOLA, with the source's onsets kept on their exact position, and everything else by [Signalsmith Stretch](https://github.com/Signalsmith-Audio/signalsmith-stretch) (MIT).

Official site with documentation can be found at:
 http://soloud-audio.com
