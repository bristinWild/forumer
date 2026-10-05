{
  description = "Forumer - Logos ui_qml module (C++ backend + QML view)";

  inputs = {
    # Pinned to 0.2.6: the builder this app is built and tested against.
    logos-module-builder.url = "github:logos-co/logos-module-builder/0.2.6";
    # Core module dependency - must match metadata.json "dependencies".
    # Pinned to v0.2.1: v0.1.x publishes no LIDL contract, which current
    # logos-module-builder requires to generate the consumer wrapper.
    delivery_module.url = "github:logos-co/logos-delivery-module/v0.2.1";
    delivery_module.inputs.logos-module-builder.follows = "logos-module-builder";
    # Core module dependency - must match metadata.json "dependencies".
    # Logos Storage: files by CID (forum history bundles). Pinned to v2.1.2:
    # v3.x needs a newer Logos SDK (LogosShutdown) than logos-module-builder
    # 0.2.6 provides; moving both together is a separate step.
    storage_module.url = "github:logos-co/logos-storage-module/v2.1.2";
    storage_module.inputs.logos-module-builder.follows = "logos-module-builder";
  };

  outputs = inputs@{ logos-module-builder, ... }:
    logos-module-builder.lib.mkLogosQmlModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;
    };
}
