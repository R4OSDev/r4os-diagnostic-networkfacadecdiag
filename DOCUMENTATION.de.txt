CNETD ist das C-Referenzprogramm fuer die typisierte Service-/Netzwerkfassade.

Es demonstriert DNS, TCP-Connect und UDP-Bind/Send ohne direkten Kernel-
Netzwerkfallback. R4NET und die jeweiligen R4X-Services muessen importiert und
verfuegbar sein.

Seit 0.58.29 ist CNETD der C-Console-Pilot des generischen R4MF-v2-Builds:

    DevTools\Scripts\Build.bat -app CNETD

Das faktfreie build.zig delegiert an `sdk.addR4MF`; das Artefakt liegt unter
`Code\zig-out\CNETD.R4X` und uebernimmt R4SYS/R4NET exakt aus dem Manifest.
