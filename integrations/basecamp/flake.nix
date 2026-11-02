{
  description = "Observer - Basecamp integration module";

  inputs = {
    logos-module-builder.url = "github:logos-co/logos-module-builder";
    chat_module = {
      url = "github:logos-co/logos-chat-module/v0.3.0";
      flake = false;
    };
  };

  outputs = inputs@{ logos-module-builder, ... }:
    let
      portableRuntimeFor = { pkgs, system }:
        let
          isDarwin = system == "aarch64-darwin";
          python = pkgs.python3.withPackages (ps: [
            ps.pyinstaller
            ps.cryptography
          ]);
        in pkgs.stdenvNoCC.mkDerivation {
        pname = "observer-portable";
        version = "0.1.5";

        src = ./runtime;
        nativeBuildInputs =
          [ python ]
          ++ pkgs.lib.optionals isDarwin [
            pkgs.darwin.system_cmds
            pkgs.cctools
            pkgs.darwin.sigtool
          ]
          ++ pkgs.lib.optionals (!isDarwin) [
            pkgs.patchelf
            pkgs.binutils
          ];

        dontConfigure = true;
        dontBuild = true;

        installPhase = ''
          runHook preInstall

          export HOME="$TMPDIR"
          export PYINSTALLER_CONFIG_DIR="$TMPDIR/pyinstaller-cache"

          mkdir -p "$out"

          pyinstaller \
            --noconfirm \
            --clean \
            --onedir \
            --name observer \
            --paths "$PWD" \
            --hidden-import=v2_pair_cli \
            --distpath "$out" \
            --workpath "$TMPDIR/pyinstaller-work" \
            --specpath "$TMPDIR" \
            ./v2_server.py

          test -x "$out/observer/observer"
          ln -s observer "$out/observer/logos-observer"

        '' + pkgs.lib.optionalString isDarwin ''
          # Repair Nix-derived OpenSSL paths after PyInstaller collection.
          internal="$out/observer/_internal"
          rust="$internal/cryptography/hazmat/bindings/_rust.abi3.so"
          ssl="$(find "$internal/lib-dynload" -name '_ssl*.so' -print -quit)"
          hashlib="$(find "$internal/lib-dynload" -name '_hashlib*.so' -print -quit)"
          legacy="$internal/ossl-modules/legacy.dylib"

          for binary in "$rust" "$ssl" "$hashlib" "$legacy"; do
            test -f "$binary"
          done

          install_name_tool -change             "@rpath/libssl.3.dylib"             "@loader_path/../../../libssl.3.dylib" "$rust"

          install_name_tool -change             "@rpath/libcrypto.3.dylib"             "@loader_path/../../../libcrypto.3.dylib" "$rust"

          install_name_tool -change             "@rpath/libssl.3.dylib"             "@loader_path/../libssl.3.dylib" "$ssl"

          install_name_tool -change             "@rpath/libcrypto.3.dylib"             "@loader_path/../libcrypto.3.dylib" "$ssl"

          install_name_tool -change             "@rpath/libcrypto.3.dylib"             "@loader_path/../libcrypto.3.dylib" "$hashlib"

          install_name_tool -change             "@rpath/libcrypto.3.dylib"             "@loader_path/../libcrypto.3.dylib" "$legacy"

          install_name_tool -id "@rpath/libssl.3.dylib"             "$internal/libssl.3.dylib"

          install_name_tool -change             "@rpath/libcrypto.3.dylib"             "@loader_path/libcrypto.3.dylib"             "$internal/libssl.3.dylib"

          install_name_tool -id "@rpath/libcrypto.3.dylib"             "$internal/libcrypto.3.dylib"

          for binary in "$rust" "$ssl" "$hashlib" "$legacy"             "$internal/libssl.3.dylib" "$internal/libcrypto.3.dylib"; do
            codesign --force --sign - "$binary"
          done

        '' + ''
          runHook postInstall
        '';
      };
    in
    logos-module-builder.lib.mkLogosModule {
      src = ./.;
      configFile = ./metadata.json;
      flakeInputs = inputs;

      postInstall = ''
        mkdir -p "$out/share/logos/observer-runtime"

        cp -v ${./runtime}/*.py \
          "$out/share/logos/observer-runtime/"

        cp -v ${./runtime}/requirements.txt \
          "$out/share/logos/observer-runtime/"

      '';

      postFixup = { pkgs, system }:
        let
          supported =
            system == "aarch64-darwin" ||
            system == "x86_64-linux";

          runtimeName =
            if system == "aarch64-darwin"
            then "portable-macos-arm64"
            else "portable-linux-x86_64";
        in pkgs.lib.optionalString supported ''
          mkdir -p "$out/share/logos/observer-runtime"

          cp -R ${portableRuntimeFor { inherit pkgs system; }}/observer \
            "$out/share/logos/observer-runtime/${runtimeName}"
        '';
    };
}
