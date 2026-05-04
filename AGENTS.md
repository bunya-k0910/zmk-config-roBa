# roBa ZMK 設定リポジトリ運用メモ

このリポジトリは、分割キーボード roBa の ZMK 設定を管理するためのものです。

## 基本情報

- リポジトリ: `/Volumes/SN770_2TB/Documents/code/zmk-config-roBa`
- GitHub: `bunya-k0910/zmk-config-roBa`
- 通常利用時のUSB接続は右側です。
- `roBa_R` が central 側、`roBa_L` が peripheral 側です。
- 左右のファームウェア書き込みは片側ずつ行います。
- `XIAO-SENSE` は XIAO のブートローダー名で、左右どちらかは判別できません。必ずユーザーに確認してください。
- 通常のキーマップ変更では `settings_reset` は書き込みません。

## 作業方針

- Keymap Editor やユーザーが行った変更を消さないでください。
- push が拒否された場合は、`git pull --rebase origin main` で取り込み、衝突を慎重に解決してください。
- `firmware/` 配下の成果物は Git 管理に含めません。
- 変更はできるだけ以下に限定します。
  - `config/roBa.keymap`
  - `boards/shields/roBa/roBa_R.overlay`
  - `boards/shields/roBa/roBa_L.overlay`
  - `boards/shields/roBa/roBa.dtsi`
  - `boards/shields/roBa/*.conf`

## ビルド手順

1. 変更をコミットします。
2. `main` に push します。
3. GitHub Actions の workflow id `270536925` を手動実行します。
4. run id を確認し、完了まで監視します。
5. 成果物を `firmware/<run_id>` にダウンロードします。

代表コマンド:

```sh
gh workflow run 270536925 --ref main
gh run watch <run_id> --exit-status
mkdir -p firmware/<run_id>
gh run download <run_id> -D firmware/<run_id>
```

## 書き込み手順

ユーザーに左右どちらが `XIAO-SENSE` としてマウントされているか確認してから書き込みます。

右側:

```sh
cp -X firmware/<run_id>/firmware/roBa_R-seeeduino_xiao_ble-zmk.uf2 /Volumes/XIAO-SENSE/
```

左側:

```sh
cp -X firmware/<run_id>/firmware/roBa_L-seeeduino_xiao_ble-zmk.uf2 /Volumes/XIAO-SENSE/
```

コピー後に `XIAO-SENSE` が消えれば、通常は書き込み成功です。

## 現在わかっている調整値

ロータリー回転のスクロールは、以下の組み合わせで動作確認済みです。

- `#define ZMK_POINTING_DEFAULT_SCRL_VAL 80`
- `left_encoder` の `steps = <24>`
- `scroll_encoder` の `tap-ms = <20>`

P + トラックボールのスクロール速度は、右側 overlay の次の値で調整します。

```dts
&zip_scroll_scaler 1 24
```

値を小さくする場合は速くなり、大きくする場合は遅くなります。

## キーマップ上の注意

- Keymap Editor 上の `encoder_left` はロータリーの回転です。
- ロータリー押し込みは、通常キーと同じようにキーマップ上のキー位置として扱われます。
- 現在は `TAB` の上にあるキー位置をロータリー押し込み候補として扱っています。
- macOS の再生/一時停止は `&kp C_PLAY_PAUSE` です。

## 日本語入力切替

- 英数/かな切替には `LANG2` と `LANGUAGE_1` を使います。
- `英数位置 + Space` のコンボで `LANGUAGE_1` を送る設定があります。
- Space が少し先に入力されても拾えるよう、コンボの `timeout-ms` を調整しています。
