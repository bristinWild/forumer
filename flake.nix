{
  description = "Forumer - Logos ui_qml module (C++ backend + QML view)";

  inputs = {
    # The 0.3 generation: what today's Basecamp loads (0.2 modules don't).
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.3.2";
    # Core module dependency - must match metadata.json "dependencies".
    # v0.3.0 is the delivery of testnet v0.3.0. Network: see bootstrap() -
    # logos.dev by default (no RLN), logos.test when chosen in Settings.
    delivery_module.url = "github:logos-co/logos-delivery-module/v0.3.0";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
    # Core module dependency - must match metadata.json "dependencies".
    # Logos Storage, the version Basecamp ships (its package downloader runs
    # a node; Forumer attaches to it there).
    storage_module.url = "github:logos-co/logos-storage-module/v3.0.0";
    storage_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
