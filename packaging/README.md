# Packaging

Stub. Installers, desktop integration and distribution packages are milestone M9 work (spec §28 M9,
§31); Stage 1 produces build trees only (plan §1.3, ADR-0021).

Constraints already fixed for whoever packages ArchMage:

- The distribution and license model is undecided (plan Q-02, ADR-0025). Qt is linked dynamically so
  no path is foreclosed; `docs/dependencies/manifest.yaml` lists each dependency's license and
  redistribution obligations.
- The desktop executable sets its desktop-file name to `archmage`, which is also its Wayland app ID.
- The native document extension is `.archmage`; `application/x-archmage` is a provisional MIME type
  pending registration (spec §10.1).
