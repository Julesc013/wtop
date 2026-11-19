# utf8proc provenance

Unmodified upstream files from **utf8proc v2.11.0**, commit
`d7bf128df773c2a1a7242eb80e51e91a769fc985`, Unicode **17.0.0**.

Source: https://github.com/JuliaStrings/utf8proc/tree/v2.11.0

The adjacent upstream `LICENSE.md` contains the MIT library licence and Unicode
data licence. Wtop statically links this dependency; no installed Unicode DLL,
package manager or runtime download is needed. The dependency is isolated in its
own CMake target and only the grapheme/width interface is used by Wtop.

Update procedure: replace the four upstream files together at a reviewed release,
update this provenance and the official Unicode grapheme conformance fixture,
run boundary, width-policy, clipping, editor and native-renderer tests, and record
the resulting Unicode version in the release notes. Do not hand-edit generated
Unicode data. This modern Unicode profile is not evidence of a DOS/Win16 build.
