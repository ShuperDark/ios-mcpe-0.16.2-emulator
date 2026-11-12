# iOS MCPE (v0.16.2) Emulator

Windows 10 / 11 (x64) 上で iOS 版 Minecraft Pocket Edition 0.16.2 (arm64) を動かすエミュレータです。
iOS 全体ではなく、ゲームが使う部分だけを実装しています。

## 仕組み

| 層 | 実装 |
|---|---|
| CPU | [dynarmic](external/dynarmic) による ARM64 → x86-64 JIT（スレッドごとに1インスタンス） |
| メモリ | ゲストのアドレス = ホストのアドレス（ポインタをそのまま共有） |
| ローダ | 自前の Mach-O ローダ（rebase / bind / lazy bind / weak bind / TLV） |
| libSystem | ホスト側 HLE（libc, stdio, pthread, mach, dyld, sysctl, BSD ソケット → Winsock） |
| OpenGL ES 2.0 | ホスト側 HLE → デスクトップ OpenGL 4.x 互換プロファイル（GLSL ES はそのまま、失敗時は変換） |
| libc++ / libc++abi / libunwind | LLVM のソースを arm64 向けにビルドしたゲスト用 dylib（Apple の ABI に合わせた設定） |
| Objective-C ランタイム | 自作（`guest/libobjc`、objc_msgSend は arm64 アセンブリ） |
| libdispatch, Foundation, CoreFoundation, UIKit, QuartzCore, EAGL, CoreGraphics, AudioToolbox など | 自作の最小実装（`guest/`、Objective-C） |
| zlib | 公式ソースをゲスト用にビルド |
| iPhone への画面出力 | 内蔵の HTTP / WebSocket サーバー（`remote.cpp`）から Safari に JPEG の映像と PCM の音声を送り、タッチと文字入力を受け取る |
| Mod | `mods\*.dylib`（arm64）をゲームと一緒に読み込み、関数フックなどの API（`hle_mod.cpp`）を提供 |

`guest/` 以下は zig 同梱の clang で arm64 Mach-O にビルドされ、エミュレータがゲームと一緒にロードします。
ホスト ↔ ゲストの橋渡しは `svc #n` スタブ（`hle.h` の `HLE_FUNC` マクロ）です。

ゲーム固有の調整：HockeyApp（クラッシュレポータ）は無効化。Xbox Live / Realms / ストアは使えません。
PC 版と同じキーボード・マウス操作、LAN マルチプレイ（IP 指定での参加も可）、iPhone のブラウザへの画面出力・タッチ操作・音声（Wi-Fi / USB）、
自分のスキン画像の取り込み、Mod が使えます（下記）。

## 必要なもの

- Windows 10 / 11（x64）
- Visual Studio 2022 / 2026 と、ワークロード「C++ によるデスクトップ開発」
  （「Windows 用 C++ CMake ツール」を含む。CMake と Ninja は Visual Studio に付属）
- Python 3（[python.org](https://www.python.org/) 版。`py` コマンドが使えること）。初回ビルドで `ziglang` を pip で自動インストールします
- 自分で用意した、復号済みの `Minecraft PE 0.16.2`（iOS 版）の .ipa

App Store から入手したままの暗号化された .ipa（FairPlay DRM）は動きません。暗号化の解除には、その Apple ID で認証された
実機の iOS が持つ鍵が必要で、Windows 上では解除できないためです（エミュレータは暗号化を検出すると、その旨を表示して終了します）。

**.ipa（ゲーム本体）はこのリポジトリにも、ビルドした exe にも含まれていません。** 著作権の都合で配布できないため、各自で用意して `ipa\` フォルダに置いてください。
エミュレータは起動時に、exe のフォルダから親フォルダへ順に `ipa\*.ipa` を探します（`--ipa <ファイル>` で直接指定も可）。
ゲームは初回起動時に `%LOCALAPPDATA%\iOS-MCPE-0.16.2\app\` に展開されます。

## ソースからのビルドとデバッグ（Visual Studio）

GitHub から取得した場合も、この手順でビルド・デバッグできます。

### 1. 準備

```bat
git clone <このリポジトリの URL> "iOS MCPE(v0.16.2) Emulator"
```

`ipa\` フォルダに .ipa を置きます（例: `ipa\Minecraft PE 0.16.2.ipa`。ファイル名は自由です）。
外部ソース（dynarmic とその依存ライブラリ、Boost ヘッダ、zlib、minimp3、stb_vorbis）は `external\` に入っているので、追加のダウンロードは不要です。

### 2. Visual Studio で開く

1. Visual Studio を起動し、「ローカル フォルダーを開く」（または「ファイル → 開く → フォルダー」）でリポジトリのフォルダを開きます。
   （CMake プロジェクトです。`.sln` で開きたい場合は下の「ソリューションで開く」を参照）
2. CMake の構成が自動で始まります。出力ウィンドウに `CMake generation finished` と出るまで待ちます（初回は数分）
3. ツールバーで構成を選びます
   - `x64 Debug`：エミュレータ本体を最適化なしでビルド（ブレークポイント・ステップ実行用。JIT だけは最適化されるので、動作は Release とほぼ同じ速さです）
   - `x64 Release`：遊ぶとき・速度を測るとき
   - `Visual Studio solution (.sln / .slnx)` はフォルダー表示では使いません（下の `make_sln.bat` 用）
4. スタートアップ項目（▶ の横）で `iOSMCPE-Emulator.exe` を選びます（CMake のターゲット名は `CMakeTarget`）

#### ソリューション（.slnx / .sln）で開く

従来の Visual Studio ソリューションとして開きたい場合は、リポジトリの `make_sln.bat` をダブルクリックします。
`out\build\vs-solution\` にソリューションが作られ、Visual Studio で開きます
（Visual Studio 2026 では `iOS MCPE(v0.16.2) Emulator.slnx`、2022 では `.sln`。インストールされている最新の Visual Studio が使われます）。

- スタートアップ プロジェクトは最初から `CMakeTarget`（`iOSMCPE-Emulator.exe`）なので、構成（`Debug` / `Release`）を選んで F5 でデバッグできます
- ゲスト用ライブラリのビルドとコピーも、ソリューションのビルドに含まれています
- できた exe：`out\build\vs-solution\iOS MCPE(v0.16.2) Emulator\Debug\iOSMCPE-Emulator.exe`（Release は `Release\`）
- コマンドライン引数は、ソリューション エクスプローラーで `CMakeTarget` を右クリック →「プロパティ → デバッグ → コマンド引数」に書きます
- `CMakeLists.txt` を変更したときは、ビルド時に自動で再生成されます（ファイルを追加したときも `CMakeLists.txt` に書けば反映されます）

### 3. ビルド・実行・デバッグ

| 操作 | 方法 |
|---|---|
| ビルド | 「ビルド → すべてビルド」（Ctrl+Shift+B） |
| デバッグ実行 | F5（ブレークポイント、例外での停止、変数の確認が使えます） |
| デバッグなしで実行 | Ctrl+F5 |
| ビルドのやり直し | 「プロジェクト → CMake キャッシュを削除して再構成」→ すべてビルド |

- ゲスト用ライブラリ（`guest\`、zig でビルドする自作の iOS ランタイム）も、同じビルドの中で `guest\build.py` が自動的にビルドします
- できた exe：`out\build\x64-debug\iOS MCPE(v0.16.2) Emulator\iOSMCPE-Emulator.exe`（Release は `x64-release`）。
  同じフォルダに `guest\`（ゲスト用ライブラリ）と `remote.html`（iPhone 用ページ）がコピーされます
- コマンドライン引数や環境変数を付けてデバッグするには、ソリューション エクスプローラー（フォルダー表示）で
  ルートの `CMakeLists.txt` を右クリック →「デバッグと起動の設定を追加」→ `iOSMCPE-Emulator.exe` を選び、作られた `launch.vs.json` に書きます

```json
      "args": [ "--data", "${env.LOCALAPPDATA}\\iOS-MCPE-0.16.2-2" ],
      "env": { "EMU_SHOW_FPS": "1" }
```

  （Visual Studio が作った構成の中に、上の `args` / `env` を追記します）

- ブレークポイントが効くのはエミュレータ本体（`iOS MCPE(v0.16.2) Emulator\` の C++）だけです。
  ゲームと `guest\` のコードは arm64 として JIT で実行されるため、Visual Studio では止められません（`emulator.log` のログで確認します）
- ゲームのクラッシュ時は `emulator.log` にゲスト側のレジスタとバックトレースが出ます

コマンドラインでビルドする場合（Visual Studio は開かなくてよい）：

```bat
build.bat            :: Release
build.bat debug      :: Debug
make_sln.bat         :: Visual Studio ソリューションを作って開く
```

### デバッグ実行を軽くするには

Debug 構成ではエミュレータ本体は最適化なし、JIT（dynarmic とその依存ライブラリ）だけ最適化してビルドします。
JIT まで最適化なしにすると、ゲームのコードを初めて実行するたびの変換に数秒かかり、起動が 15 秒、ワールドの読み込みが 45 秒ほどになるためです（現在はどちらも Release とほぼ同じ）。

- 遊ぶだけなら構成を `x64 Release` にする（Release でもシンボル付きなので、デバッガでの例外の確認やスタックトレースは使えます）
- 「デバッグ → オプション → 全般 → デバッグ中に診断ツールを有効にする」をオフにする（デバッグ中の CPU / メモリの常時計測が止まります）
- 出力ウィンドウ / コンソールに大量のログを出す `-v` は付けない

## GitHub に公開するとき

`.gitignore` で、次のものはコミットされないようにしてあります。

| 除外するもの | 理由 |
|---|---|
| `ipa\*.ipa`（`*.ipa` すべて） | ゲーム本体（著作物）。`ipa\README.txt` だけを残します |
| `out\` | ビルド結果（exe、ゲスト用ライブラリ）。各自がビルドします |
| `.vs\`, `CMakeUserPresets.json` | Visual Studio の個人設定・キャッシュ |
| `mod-template\out\` | Mod のビルド結果 |
| `external\boost.tar.gz` | 展開済みの `external\boost_1_86_0\` があれば不要（142MB で GitHub の 1 ファイル 100MB 制限を超えます） |

ゲームのデータ（展開したゲーム、ワールド、設定）は `%LOCALAPPDATA%\iOS-MCPE-0.16.2\` にあり、リポジトリの中には入りません。

**`external\` の中の git リポジトリについて**：`external\dynarmic`（とその `externals\` の中の fmt, mcl, robin-map, xbyak, zycore, zydis）と
`external\zlib` は、それぞれ取得元の git リポジトリのままになっています。そのまま `git add` すると中身ではなく
「空のサブモジュール」として記録され、clone した人がビルドできません。また dynarmic にはこのエミュレータ用の変更
（`src\dynarmic\backend\x64\a64_interface.cpp`）があります。公開するリポジトリに中身ごと入れるため、最初のコミットの前に
それらの `.git` を削除してください（取得元の履歴は不要になります。リポジトリのフォルダごとバックアップしてから行うと安全です）。

PowerShell で（`.git` はフォルダのものとファイルのもの〈サブモジュール〉の両方があります）：

```powershell
cd "<リポジトリのフォルダ>"
Get-ChildItem external -Recurse -Force -Filter .git | Remove-Item -Recurse -Force
git init
git add .
git status   # ipa\*.ipa や out\ が含まれていないことを確認
git commit -m "Initial commit"
```

exe を配布する場合（GitHub の Releases など）は、`iOSMCPE-Emulator.exe` と同じフォルダの `guest\` と `remote.html` を一緒に入れ、.ipa は入れないでください。
使う人は exe の隣（またはその親フォルダ）の `ipa\` に自分の .ipa を置きます。

第三者のソースのライセンス：dynarmic（0BSD）、fmt / mcl / robin-map / zycore / zydis（MIT）、xbyak（BSD-3-Clause）、
Boost（Boost Software License 1.0）、zlib（zlib License）、minimp3（CC0）、stb_vorbis（MIT またはパブリックドメイン）。ゲスト用 libc++ は zig に同梱の LLVM libc++（Apache-2.0 WITH LLVM-exception）からビルドします。
このプロジェクト自体（`external\` 以外）は MIT License です（`LICENSE`）。

## 実行

```bat
"out\build\x64-release\iOS MCPE(v0.16.2) Emulator\iOSMCPE-Emulator.exe"
```

初回は `ipa/` の .ipa を展開します（下記の `app\`）。

### データの保存先

すべて `%LOCALAPPDATA%\iOS-MCPE-0.16.2\`（例: `C:\Users\<ユーザー名>\AppData\Local\iOS-MCPE-0.16.2\`）の下です。`--data <フォルダ>` で起動すると、そのフォルダが代わりに使われます。

| 場所 | 内容 |
|---|---|
| `controls.ini` | **キー設定・マウス感度・フルスクリーン・フレームレート・画面の種類・iPhone への画面出力**（「キー設定」の節を参照） |
| `emulator.log` | ログ（起動のたびに作り直し） |
| `mods\` | Mod（`*.dylib`。「Mod の作り方」の節を参照。無ければ作ってください） |
| `app\Payload\minecraftpe.app\` | .ipa から展開したゲーム本体（読み取り専用として扱います。消すと次回起動時に展開し直します） |
| `home\` | ゲームから見た iOS のアプリのホームフォルダ |
| `home\Documents\games\com.mojang\minecraftWorlds\` | **ワールド**（1 フォルダ = 1 ワールド。名前は `levelname.txt`） |
| `home\Documents\games\com.mojang\minecraftpe\options.txt` | ゲーム内の設定（GUI スケール、描画距離、プレイヤー名 `mp_username` など） |
| `home\Documents\games\com.mojang\minecraftpe\custom.png` | 自分で選んだスキン（「スキンの変更」の節を参照） |
| `home\Documents\games\com.mojang\minecraftpe\external_servers.txt` | IP 指定で追加したサーバーの一覧 |
| `home\Documents\games\com.mojang\minecraftpe\clientId.txt` | この端末のクライアント ID |
| `home\Documents\games\com.mojang\minecraftpe\global_resource_packs.json` | 有効なリソースパックの一覧 |
| `home\Documents\games\com.mojang\resource_packs\` / `behavior_packs\` | 追加したリソースパック / ビヘイビアパック |
| `home\Documents\internal\` | ゲームの内部データ |
| `home\Library\Preferences\com.mojang.minecraftpe.plist` | アプリの設定（NSUserDefaults） |
| `home\Library\Keychain.plist` | キーチェーン（端末 ID など） |
| `home\Library\Caches\`, `home\tmp\` | キャッシュ・一時ファイル（消しても問題ありません） |

エミュレータ自体のファイル：`iOSMCPE-Emulator.exe` と同じフォルダの `guest\`（ゲームと一緒に読み込む自作の iOS ランタイム）と `remote.html`（iPhone に表示するページ）。

オプション: `--ipa <file>` / `--app <展開済み .app>` / `--data <データフォルダ>` / `--guest-libs <フォルダ>`（既定は exe の隣の `guest\`）/ `-v`（詳細ログ）/ `-vv`（さらに詳細）

### 操作（PC 版と同じ配置）

ワールドの中ではマウスカーソルが隠れてウィンドウ内に固定され、ゲームはキーボード＋マウスのモード（画面中央に照準、タッチ用ボタンなし）になります。
画面（インベントリ・ポーズ・チャットなど）を開くとカーソルが戻り、マウスはゲームのマウス操作として扱われます（アイテムを掴んで移動、ホイールでスクロール）。
ウィンドウからフォーカスが外れると、押していたキーとマウスは自動的に離されます。

| 入力 | 動作 |
|---|---|
| マウス移動 | 視点移動 |
| 左クリック（長押し） | ブロック破壊 / 攻撃 |
| 右クリック | ブロック設置 / 使う |
| ホイール | ホットバーの選択（画面表示中はスクロール） |
| W A S D | 移動 |
| Space | ジャンプ |
| Shift | スニーク（押している間だけ） |
| Ctrl | ダッシュ |
| E | インベントリを開く / 閉じる（E や右クリックで開いたチェスト・作業台なども E で閉じる） |
| T | チャット（Enter で送信、Esc で閉じる） |
| Q | アイテムを捨てる |
| 1〜9 | ホットバーのスロット選択 |
| F5 | 視点切り替え |
| Esc | ポーズ / 開いている画面を閉じる |
| 画面表示中の左クリック | 選択・アイテムを掴む / 置く |
| F11 / Alt+Enter | フルスクリーン切り替え |

キー入力はゲームが内部に持っているキーボード／マウス入力処理（Windows 10 版と共通のコード）に直接渡しています。
iOS 版の割り当てには移動キーなどが無いため、ワールド内の割り当てに上のキーを追加しています（`patches.cpp`）。

### キー設定（controls.ini）

初回起動時に `%LOCALAPPDATA%\iOS-MCPE-0.16.2\controls.ini` が作られます。メモ帳などで編集してください。

```ini
[controls]
forward     = W            ; 複数のキーはカンマ区切り (例: W, Up)
jump        = Space
sneak       = Shift
attack      = MouseLeft    ; MouseLeft / MouseRight / MouseMiddle も使えます
use         = MouseRight
inventory   = E
...
[mouse]
sensitivity = 1.0          ; 視点移動の感度
invert_y    = false        ; 上下反転
scroll_speed = 1.0         ; 画面でのホイールのスクロール量

[window]
fullscreen  = false        ; 起動時にフルスクリーン
max_fps     = auto         ; フレームレート上限 (auto = モニタのリフレッシュレート, 0 = なし)
vsync       = false        ; 垂直同期

[ui]
screens     = classic      ; classic = PC 版の画面 / pocket = スマホ版の画面

[remote]
enabled     = true         ; iPhone のブラウザへの画面出力 (false で無効)
port        = 8080         ; 待ち受けるポート (使用中なら 8081, 8082... を使います)
resolution  = 1280         ; iPhone 接続中の描画解像度 (長辺のピクセル数)
quality     = 70           ; 画質 (JPEG の品質 10-100)
fps         = 60           ; 送るフレームレートの上限
pc_audio    = false        ; iPhone 接続中も PC から音を出す (false = 音は iPhone だけ)
```

- 動作名: forward, back, left, right, jump, sneak, sprint, attack, use, inventory, chat, drop, perspective, hide_gui, hotbar1〜hotbar9
- ゲーム内部のボタン名（例: `button.sneak_toggle = Z`）も直接書けます
- `[controls]`・`[mouse]`・`[ui]`・`max_fps` は、保存したあと次に画面を開閉したとき（ワールドに入る、インベントリを閉じるなど）に反映されます
- `fullscreen`・`vsync`・`[remote]` は、次回の起動時に反映されます
- Esc（ポーズ / 画面を閉じる）は固定です。ファイルを消すと初期設定（説明付き）で作り直されます
- 書かれていない項目は初期値になります

### 画面の種類（classic / pocket）

iOS 版は本来スマホ向けの「ポケット版」画面を使いますが、ゲーム内には Windows 10 版と同じ「PC 版」画面も入っています。
既定では PC 版画面に切り替えています（インベントリでアイテムをカーソルで掴んで移動、クリエイティブのタブと検索）。
iPhone が接続している間は、この設定にかかわらずポケット版の画面になります（下記）。
文字やボタンが小さい／大きい場合は、ゲームの「設定 → ビデオ → GUI スケール」で調整してください。

### フルスクリーンとフレームレート

F11 または Alt+Enter でボーダーレスのフルスクリーンに切り替わり、ゲームの描画解像度もモニタに合わせて変わります。
フレームは 1ms 精度のタイマーで `max_fps` に合わせて出します。既定（auto）ではモニタのリフレッシュレート（例: 165Hz なら 165fps）です。
iPhone が接続している間は、`max_fps` にかかわらず 60fps になります（下記）。

### マルチプレイ（LAN）

同じネットワーク（同じ PC を含む）にいる MCPE 0.16 系のゲームと遊べます。

- **ホスト**：ワールドを開くだけで LAN に公開されます（ワールド設定の「マルチプレイ」がオンの場合）
- **参加**：「遊ぶ」→「フレンド」タブの「LAN Games」に出るワールドをクリック
- **IP アドレスを指定**：「フレンド」タブの「Add Friend」の右にあるサーバーのアイコン →「Add External Server」で、
  Server Name（任意の名前）、Server Address（IP アドレスまたはホスト名）、Port（既定 19132）を入力し、
  「Play」で接続（「Save」は一覧に追加するだけ）。追加したサーバーは「フレンド」タブに残り、次からはクリックで接続できます

同じ PC で 2 つ起動して遊ぶ場合は、2 つ目を別のデータフォルダで起動してください（同じデータフォルダは同じ端末として扱われます）。

```bat
iOSMCPE-Emulator.exe --data "%LOCALAPPDATA%\iOS-MCPE-0.16.2-2"
```

プレイヤー名は「設定 → プロフィール」で変えられます。Windows のファイアウォールの確認が出たら「プライベート ネットワーク」を許可してください。

### iPhone に画面を出して操作する

PC で動いているゲームの画面を、同じ Wi-Fi にいる iPhone の Safari に表示し、iPhone のタッチと日本語キーボードで操作できます。
iPhone にアプリを入れる必要はありません。

1. PC でエミュレータを起動します。ウィンドウのタイトルに `iPhone: http://192.168.x.x:8080` のようにアドレスが出ます（`emulator.log` にも出ます）
2. iPhone の Safari でそのアドレスを開きます（初回、Windows のファイアウォールの確認が出たら「プライベート ネットワーク」を許可）
3. 全画面で使うには、Safari の共有ボタン →「ホーム画面に追加」で追加したアイコンから開きます（アドレスバーが消えます）

接続している間は：

- ゲームの描画解像度が iPhone の画面の縦横比（長辺 1280 ピクセル）に変わり、PC のウィンドウには同じ画面が黒帯付きで表示されます
- ゲームはスマホ版の表示になります：画面はポケット版、ワールド内は十字キー・ジャンプなどのタッチ操作。
  接続したときに開いていた画面は、閉じて開き直すとポケット版になります
- マルチタッチに対応しています（十字キーを押しながら視点移動など）。iPhone を縦に持つと画面を 90 度回して表示します
- スニークは実機と同じく、十字キー中央のボタンをダブルタップです（もう一度ダブルタップで解除）
- 接続中はゲームを 60fps で動かします（iOS と同じ。ダブルタップの判定などがフレーム数で行われるため）
- 文字入力欄（チャット、ワールド名、サーバーのアドレスなど）を選ぶと iPhone の画面上部に入力欄が出ます。
  それをタップすると iPhone のキーボードが開きます（iOS は、タップしないとキーボードを開けない仕様です）。
  日本語入力（変換）も使え、キーボードの改行キーがゲームの Enter です
- PC のマウス操作はゲームに送りません（キーボードは使えます）。iPhone を閉じる / Safari を閉じると PC 版の表示と操作に戻ります
- 音は iPhone から出ます（最初に画面をタップしたときに鳴り始めます。iOS は、タップするまでページの音を出せない仕様です）。
  接続中は PC の音は止まります（`[remote]` の `pc_audio = true` で両方から出ます）。iPhone のマナーモード中も鳴ります
- 接続できる iPhone は 1 台で、別の端末が接続すると前の端末は切断されます

ワールド一覧のワールドは、1 回目のタップで選択され、もう 1 回タップすると開きます（ゲーム側の動作です）。

iTunes の「Wi-Fi 経由でこの iPhone と同期」（iPhone との同期・バックアップ用の接続）には、PC の画面を iPhone に出したり、
iPhone から PC を操作したりする機能がありません。そのため Wi-Fi 同期は使わず、iPhone のブラウザに直接映像を送っています。
同期の設定がオン／オフどちらでも使えます。

接続できるのは同じ LAN 内（プライベート IP アドレス）の端末だけです。LAN 内の誰でもアドレスを開けば操作できるので、
不要なときは `controls.ini` の `[remote]` で `enabled = false` にしてください。
映像が重い / カクつくときは `resolution`（例: 960）や `quality`（例: 50）を下げてください。

#### ケーブル（USB）で接続する

Wi-Fi より遅延が少なくなります。iPhone 側にアプリは不要ですが、iPhone の「インターネット共有」（テザリング）を USB で使います。
これは、ケーブルだけでは iPhone の Safari から PC に接続する経路がなく、iPhone と PC の間にネットワークを作る方法が
インターネット共有しかないためです（iTunes の同期用の接続は、iPhone から PC へは使えません）。

1. iPhone を PC にケーブルで接続し、iPhone の「設定 → インターネット共有 → ほかの人の接続を許可」をオンにします
   （「このコンピュータを信頼しますか？」と出たら「信頼」）。PC に「Apple Mobile Device Ethernet」というネットワークが追加されます
2. 数秒後、エミュレータのウィンドウのタイトルが `iPhone USB: http://172.20.10.x:8080` になります（`emulator.log` にも出ます）。
   タイトルに `(USB: iPhone のインターネット共有がオフ)` と出ている場合は、ケーブルはつながっていますが iPhone 側で共有が
   有効になっていません（「ほかの人の接続を許可」がオフ、モバイルデータ通信がオフ、など）。`(USB: iPhone からのアドレス待ち)` は数秒待ってください
3. iPhone の Safari でその `USB:` のアドレスを開きます。接続中のタイトルは `iPhone 接続中 (USB)` になります

注意：

- インターネット共有は携帯電話会社の契約・SIM によっては使えません（設定に項目が出ない場合など）
- 数分で USB 接続が切れる場合は、Windows の USB の省電力機能で切断されている可能性があります（管理者権限が必要）：
  - 「コントロール パネル → 電源オプション → プラン設定の変更 → 詳細な電源設定の変更 → USB 設定 → USB のセレクティブ サスペンドの設定」を「無効」
  - 「デバイス マネージャー → ネットワーク アダプター → Apple Mobile Device Ethernet → プロパティ → 電源の管理」の
    「電力の節約のために、コンピューターでこのデバイスの電源をオフにできるようにする」をオフ
  - 切れてしまったら、iPhone の「ほかの人の接続を許可」を一度オフにしてからオンにすると、つながり直します
  - USB 接続がつながった / 切れた時刻は、Windows の「イベント ビューアー → アプリケーションとサービス ログ → Microsoft → Windows →
    NetworkProfile → Operational」でも確認できます
- Windows はこの新しいネットワークを「パブリック ネットワーク」として扱うことがあります。つながらないときは、
  Windows の「設定 → ネットワークとインターネット → イーサネット（Apple Mobile Device Ethernet）」で「プライベート ネットワーク」にするか、
  ファイアウォールの確認で「パブリック ネットワーク」も許可してください
- エミュレータの通信は PC と iPhone の間だけで、モバイル通信は使いません。ただし PC に有線 LAN / Wi-Fi が無いと、
  Windows がインターネット接続に iPhone のモバイル通信を使います

### スキンの変更

自分で用意したスキン画像（64×64 または 64×32 の PNG）を使えます。

1. タイトル画面のプレイヤーの下にあるハンガーのボタン → 「Default」の 3 つ目（灰色の人形 / 前に選んだスキン）→「Choose New Skin」
2. 画像を選ぶ画面が出ます
   - PC：Windows のファイル選択画面
   - iPhone 接続中：iPhone に「画像を選ぶ」が出て、iPhone の写真やファイルから選べます
     （写真アプリ経由だと透明部分が失われることがあるので、「ファイル」から選ぶのがおすすめです）
3. 体型（腕の太さ）を選び、「Confirm」で決定します

選んだスキンは `custom.png` として保存され、次回の起動後もそのまま使われます。

## Mod の作り方

Mod はゲームと同じ形式（arm64 の Mach-O dylib）のプログラムで、`mods\` フォルダに置くとゲームと一緒に読み込まれます。
`__attribute__((constructor))` を付けた関数がゲームの初期化より先に実行され、ゲームと同じ関数（libc、libc++、
Foundation / Objective-C ランタイム、ゲーム自身の関数）と、エミュレータの Mod 用関数（`mod-template\include\emu_mod.h`：
ログ出力 `emu_log`、関数フック `emu_hook`、`emu_game_base`、`emu_set_title`、画像の選択 `emu_pick_media_begin` /
`emu_pick_image_poll` / `emu_pick_image_take` / `emu_pick_image_cancel`（PC ではファイル選択画面、iPhone 接続中は
iPhone の写真・ファイル選択。PNG / JPEG / BMP / GIF のファイルの中身をそのまま受け取ります）、MMD モデルの選択
`emu_pick_model_begin`（PC で .pmx / .pmd を選ぶとフォルダごと ZIP にまとめて渡し、.zip はそのまま渡します。iPhone 接続中は
ファイルアプリで選んだ .zip などをそのまま渡します。受け取りは画像と同じ `emu_pick_image_poll` / `emu_pick_image_take`）、
音声ファイル（.mp3 / .ogg）の選択 `emu_pick_sound_begin`、音声の再生 `emu_sound_load` / `emu_sound_play` / `emu_sound_stop` /
`emu_sound_playing` / `emu_sound_free`（MP3 / Ogg Vorbis をエミュレータ側でデコードし、ゲームの音声に混ぜて PC と iPhone に出します））を使えます。

### Visual Studio のプロジェクト

**`mod-template\` フォルダをコピーして使うのが一番簡単です**（Visual Studio の「ローカル フォルダーを開く」で開き、
構成「MCPE mod (arm64, Release)」を選んで「すべてビルド」。できた `example_mod.dylib` は自動で `mods\` にコピーされます）。

Visual Studio の「新しいプロジェクトの作成」から作る場合は、テンプレート **「CMake プロジェクト」**（C++ / Windows・Linux、
英語版では "CMake Project"）を選んでください。作成後、`mod-template\` の `CMakeLists.txt`・`CMakePresets.json`・`cmake\`・
`include\`・`src\` で置き換えます（Visual Studio が作った `CMakePresets.json` は削除）。

- 「コンソール アプリ」「ダイナミック リンク ライブラリ (DLL)」などの MSVC のテンプレートは使えません。
  これらは Windows（x64）のプログラムを作りますが、Mod はゲーム内（arm64 の JIT 上）で動くため、
  `cmake\zig-arm64-ios.cmake` のツールチェーン（エミュレータのゲスト用ライブラリと同じ zig 付属の clang）でビルドする必要があります
- 必要なのは Python と `py -m pip install ziglang` だけです（エミュレータのビルドで入っていればそのまま使えます）
- C++ の標準ライブラリはエミュレータの libc++（ゲームと同じ ABI）を使います。`std::string` などはそのまま使えます
- Mod はゲーム内で JIT 実行されるため、Visual Studio のブレークポイントは効きません。`emu_log` / `EMU_LOGF` で `emulator.log` に出力して確認してください
- ゲームの関数のアドレスは、逆アセンブラで `minecraftpe`（ベースアドレス 0x100000000）を開いて調べます（`emu_hook` の制限は `emu_mod.h` を参照）

## デバッグ用環境変数

| 変数 | 内容 |
|---|---|
| `EMU_WINDOW_SIZE=1600x900` | ウィンドウサイズ（ピクセル） |
| `EMU_SCREENSHOT=<dir>` / `EMU_SCREENSHOT_EVERY=<秒>` | フレームを BMP で保存 |
| `EMU_AUTOTAP="25:640,370;90:key=E;92:keydown=W;94:keyup=W;95:mouse=100,0;96:mbutton=1,1"` | 指定秒にタップ / キー / マウス入力（自動テスト用） |
| `EMU_WATCHDOG=<秒>` | 定期的に全ゲストスレッドのレジスタ/バックトレースを出力 |
| `EMU_TRACE_EXCEPTIONS=1` | ホスト側の first-chance 例外と直前の GL 呼び出しを出力 |
| `EMU_LOOP_STATS=1` | メインループ/描画回数の統計 |
| `EMU_NO_AUDIO=1` | 音声を無効化 |
| `EMU_NO_MOUSELOOK=1` | ワールド内でもマウスを固定しない（マウスでの視点操作は無効になります） |
| `EMU_NO_GAME_KEYS=1` | キーをゲームに渡さない |
| `EMU_TOUCH_MENUS=1` | 画面表示中のマウスをタッチとして扱う（以前の動作） |
| `EMU_SHOW_FPS=1` | タイトルバーにフレームレートを表示（`-v` ではログにも出力） |
| `EMU_ACCURATE_FP=1` | JIT の浮動小数点の高速化（NaN・FPCR の厳密な再現の省略）を無効化 |
| `EMU_PROFILE=<秒>` | サンプリングプロファイラ。指定秒ごとにスレッド別の内訳（ゲームのコード / JIT コンパイラ / HLE / OS）、時間のかかった HLE 関数、50ms を超えたフレームを `emulator.log` に出力 |
| `EMU_NCPU=<数>` | ゲームに見せる CPU 数（地形生成などのワーカースレッド数。既定は論理 CPU 数の半分、2〜8） |
| `EMU_OFFLINE=1` | ネットワークに接続していないことにする（LAN マルチプレイも無効） |
| `EMU_NO_IFADDRS=1` | ゲームにネットワークアダプターの一覧を見せない（LAN の問題の切り分け用） |
| `EMU_TRACE_KEYCHAIN=1` | キーチェーン（端末 ID などの保存）へのアクセスをログに出力 |

## 既知の制限

- 起動やワールドの読み込み、初めて開く画面では、ゲームのコードを初めて JIT コンパイルするため一時的に止まることがあります
- Xbox Live サインイン、Realms、ストア（スキンパックの購入を含む）は使えません。自分で用意したスキン画像は使えます（「スキンの変更」を参照）
- 稀にタイミング依存のクラッシュが起きることがあります（`emulator.log` に状態が出ます）
