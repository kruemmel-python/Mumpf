# Mumpf für Windows 11
<img width="1376" height="768" alt="mumpf-cover-16x9" src="https://github.com/user-attachments/assets/f8b98849-6253-4035-a2c1-ef48bd0e08bf" />

**Mumpf** ist ein natives, leichtgewichtiges Windows-11-Desktop-Helferlein in **C++20** (Win32, Direct2D, DirectWrite, Windows UI Automation, Shell COM & WinHTTP). Er sitzt als per-pixel transparentes Overlay ohne Fokus-Klau am Rand des aktiven Fensters, beobachtet den Arbeitskontext und hilft auf Klick mit deinem **lokalen LLM in LM Studio** weiter.

---

## Highlights

* **Kein Fokus-Klau & flüssiges Docking:** Sitzt transparent am aktiven Fenster (`WS_EX_NOACTIVATE`), gleitet bei Fensterwechseln mit und lässt Klicks außerhalb seiner Silhouette durch.
* **Lokales LLM (LM Studio, Auto-Modellerkennung):** Fragt automatisch `GET /v1/models` ab und nutzt das aktuell geladene Modell (inkl. 90-s-Timeout & 600 Tokens für Reasoning-Modelle wie `google/gemma-4-e2b` oder `phi-3-mini-4k-instruct`).
* **Spontane LLM-Sprüche alle 2 Minuten:** Im Leerlauf haut Mumpf alle 2 Minuten einen flachen Witz, einen Praxis-Tipp oder einen lockeren Spruch zum gerade genutzten Programm heraus — mit **dynamisch wachsender Sprechblase** und **10 Sekunden Animation** (im Tray-Menü umschaltbar).
* **Systemweite Text-Markierung:** Markiere irgendwo Text → Klick auf Mumpf → *Erklären*, *Zusammenfassen*, *Code/Fehler analysieren*, *Übersetzen* oder *Eigene Anweisung*. Antwort erscheint in einem **dynamisch wachsenden, kopierbaren Fenster**.
* **Windows Explorer Integration (inkl. Windows-11-Tabs):** Erkennt markierte Dateien im aktiven Tab (`IShellView`), zeigt Dateieigenschaften (+ Button *„Mit LLM analysieren“*), und bietet *Öffnen*, *Kopieren nach...*, *Verschieben nach...* und *Löschen (Papierkorb)*. Überwacht außerdem Kopierdialoge im Vorder- und Hintergrund.
* **Internetbrowser-Integration:** Erkennt **Edge, Chrome, Firefox, Brave, Opera und Vivaldi** und bietet per Klick *Seite zusammenfassen*, *Stichpunkte*, *Erklären*, *Frage zur Seite stellen* und *Seitentitel kopieren*.
* **Entwickler-IDEs & KR Compiler IDE:** Erkennt **KR Compiler IDE**, **Antigravity IDE**, **Codex**, **VS Code** und **Visual Studio**. Über die Named Pipe `\\.\pipe\mumpf-events` (NDJSON) reagiert Mumpf live auf Build-Start, Warnungen, Compilerfehler und erfolgreiche Rebuilds.

---

## Installation (MSI-Installer)

Der fertige Windows-Installer liegt unter `dist\Mumpf-Setup-1.0.0-x64.msi`:

* **Standard-Installation:** Doppelklick auf `dist\Mumpf-Setup-1.0.0-x64.msi`
* **Installation auf ein anderes Laufwerk (z. B. `H:`, ohne `C:` zu belegen):**
  ```powershell
  msiexec /i "H:\mumpf_for_Windows\dist\Mumpf-Setup-1.0.0-x64.msi" INSTALLFOLDER="H:\Programme\Mumpf"
  ```

---

## Schnellstart (ohne Installation)

1. Optional **LM Studio** starten, ein Modell laden und den lokalen Server (`Port 1234`) aktivieren.
2. Mumpf starten:
   ```powershell
   Start-Process "H:\mumpf_for_Windows\build\Release\mumpf.exe"
   ```
3. KR-Compiler-IDE-Demo testen:
   ```powershell
   powershell -ExecutionPolicy Bypass -File "H:\mumpf_for_Windows\scripts\simulate_kr_build.ps1"
   ```

---

## Selber bauen (strikt auf `H:`, kein Temp/Cache auf `C:`)

```powershell
# Binary bauen (H:\mumpf_for_Windows\build\Release\mumpf.exe)
powershell -ExecutionPolicy Bypass -File "H:\mumpf_for_Windows\build.ps1"

# MSI-Installer bauen (H:\mumpf_for_Windows\dist\Mumpf-Setup-1.0.0-x64.msi)
powershell -ExecutionPolicy Bypass -File "H:\mumpf_for_Windows\installer\build_msi.ps1"
```

---

## Vollständige Dokumentation

Alle Details zu Architektur, Named-Pipe-Protokoll, Konfiguration (`mumpf.config.json`), Spritesheet-Layout sowie die vollständige Liste aller unterstützten Programme inkl. Download-Links findest du in:

* **[Docs/Dokumentation.md](Docs/Dokumentation.md)**
