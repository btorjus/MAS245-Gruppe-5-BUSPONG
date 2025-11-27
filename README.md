# CAN‑Pong — Kort prosjektbeskrivelse

Dette er en todelt CAN‑basert «pong» for to Teensy 3.6 med SSD1306‑skjermer og joystick. Hver enhet sender egen paddleposisjon (1 byte, 0–63) på CAN (gruppeID+20). Første lokale joystick‑trykk gjør enheten til master som simulerer ballen og sender ballposisjon (2 byte) på CAN (gruppeID+50). Skjermen oppdateres ~30 FPS, paddle sendes ~25 Hz og master sender ball på 100 Hz.

Maskinvare: Teensy 3.6 (SK Pang), SSD1306 (SPI), joystick, CAN‑transceivere; CAN‑baud = 500 kbps.  
Kjøring: bygg med PlatformIO eller Arduino IDE, last opp til Teensy, koble to enheter på samme CAN‑buss og start.  
Begrensninger: master‑valg skjer lokalt (ingen CAN‑election) og meldinger mangler node‑ID — dette kan gi dobbel‑master og loopback‑forvirring. Anbefalt forbedring: legg til NODE_ID i meldinger og implementer enkel master‑election (eller MASTER_CLAIM).

Kritiske filer: `src/main.cpp` (hovedlogikk), avhengigheter: FlexCAN_T4, Adafruit_SSD1306, Adafruit_GFX.  
For rask verifisering: slå på seriell logging i koden for å se sendte CAN‑ID og payload ved kjøring.
