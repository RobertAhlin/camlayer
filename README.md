# CamLayer

**AI Background Removal for OBS Studio**

CamLayer är ett projekt för att frilägga en person från en webbkamera i realtid med AI, utan green screen. Målet är att ge en mjuk, stabil och naturlig bakgrundsborttagning med bildkvalitet jämförbar med Microsoft Teams, och på sikt kunna använda resultatet som en transparent källa i OBS Studio.

> **Status:** Planering. CamLayer v0.1 är ännu inte implementerad.

## Mål

- Frilägg personen i kamerabilden i realtid.
- Behåll genomskinlig bakgrund (alfakanal) för flexibel komposition.
- Prioritera bildkvalitet, stabila konturer och låg fördröjning.
- Kör kamerabehandlingen lokalt på användarens dator, utan att skicka videoströmmen till en server.
- Utveckla och testa en fristående webbapp innan vi väljer lösning för OBS-integrationen.

Microsoft Teams används som **visuell kvalitetsreferens**, inte som en teknisk implementation eller en garanti för samma resultat.

## Planerad MVP: v0.1

Den första versionen ska köras i en vanlig webbläsare och innehålla:

- Starta och stoppa webbkameran efter användarens medgivande.
- Lista och välja mellan tillgängliga kameror.
- Visa originalbilden och den AI-frilagda bilden sida vid sida.
- Visa transparent bakgrund, med valbart rutmönster i förhandsvisningen.
- Justera masktröskel och kantmjukhet.
- Enkel stabilisering av personmasken mellan bildrutor.
- Valbar bearbetningsupplösning, inledningsvis 640 × 360 och 1280 × 720.
- Visa bearbetningshastighet (FPS) och ungefärlig behandlingstid.
- Hantera kamerafel och nekade behörigheter.

**Klart för v0.1** när vi kan öppna webbappen, aktivera kameran och se en rörlig person med transparent bakgrund i realtid. **OBS-integrering ingår inte i v0.1.**

## Föreslagen teknik

| Del | Teknik |
| --- | --- |
| Webbapp | TypeScript + Vite |
| Personsegmentering | MediaPipe Tasks Vision (första modellen att utvärdera) |
| Bildkomposition | Canvas 2D eller WebGL |
| Versionshantering | GitHub |
| Valfri webbpublicering | Vercel (HTTPS) |
| Målplattform på sikt | OBS Studio |

Segmenteringsmotorn ska hållas utbytbar så att andra modeller kan jämföras om MediaPipe inte ger tillräcklig kvalitet.

## Hur det är tänkt att fungera

1. Webbläsaren får kameratillstånd efter ett aktivt klick.
2. En segmenteringsmodell identifierar personen i varje bildruta.
3. Masken behandlas för mjukare och stabilare konturer.
4. Kamerabilden kombineras med masken till en transparent canvas.
5. Resultat och prestanda visas i gränssnittet.

Videobilder ska inte skickas till någon backend. Webbappen kan däremot behöva hämta programkod och modellfiler via nätverket.

## Utvecklingsmiljö

Första målmaskinen:

- Windows 11, 64-bitars
- Intel Core i5-1335U
- Intel Iris Xe Graphics
- 16 GB RAM
- Extern Sandberg-webbkamera, upp till 1920 × 1080 vid 30 fps
- OBS Studio 33.0.1, 64-bitars

Lösningen ska i första hand kunna användas utan administrativa installationsrättigheter. Vi förutsätter inte NVIDIA RTX eller särskilda GPU-funktioner.

## Planerad projektstruktur

```text
camlayer/
├── README.md
├── package.json
├── index.html
├── src/
│   ├── main.ts
│   ├── camera.ts
│   ├── segmenter.ts
│   ├── compositor.ts
│   ├── ui.ts
│   └── styles.css
├── public/
└── docs/
    ├── architecture.md
    └── testplan.md
```

Strukturen är planerad och filerna skapas under utvecklingen.

## Färdplan

| Version | Innehåll |
| --- | --- |
| **v0.1** | Webbkameraflöde, AI-segmentering, transparent förhandsvisning, grundläggande justering och prestandamätning |
| **v0.2** | Jämföra modeller och förbättra hår, händer, kanter, flimmer samt lokalt sparade inställningar |
| **v0.3** | Undersöka och testa integration med OBS Studio |
| **v1.0** | Stabilt användarflöde, verifierad bildkvalitet och prestanda, dokumentation |

## Testning och öppna frågor

Vi ska jämföra med Teams under samma ljusförhållanden och kamera, särskilt vid:

- Hår och axlar mot ljus respektive mörk bakgrund.
- Snabba rörelser med huvud och händer.
- Föremål nära kroppen och händer framför ansiktet.
- Svag belysning.
- Flimmer, konturfel, ljusa halos och rörelsesläp.

Vi ska även mäta FPS, fördröjning och CPU/GPU-belastning.

**OBS-kompatibilitet behöver verifieras separat.** En transparent canvas i Chrome eller Edge garanterar inte att OBS Browser Source kan använda kameran eller bevara transparensen på samma sätt. Kameran kanske inte heller kan öppnas av flera program samtidigt. Därför beslutas integrationsmetod först efter praktiska tester.

## Kom igång

Projektet är ännu inte körbart. Nästa steg är att skapa Vite-projektet med TypeScript, implementera kamerastart och därefter lägga till AI-segmentering.

När den första implementationen finns dokumenteras installationskommandon och körinstruktioner här.

## Integritet och licenser

CamLayer ska behandla kamerabilder lokalt. Ingen automatisk videouppladdning eller inspelning ingår i MVP. Licenser för kodbibliotek och modellfiler måste kontrolleras innan eventuell distribution. Datorns IT-policy och kamerabehörigheter ska respekteras.

---

**Projektstart:** 4 november 2026  
**Aktuell milstolpe:** CamLayer v0.1 MVP
