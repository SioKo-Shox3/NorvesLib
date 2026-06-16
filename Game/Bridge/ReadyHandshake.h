#pragma once

#include <cstdint>

// Workstream L-P2a: READY ハンドシェイクヘルパ。
//
// NorvesEditor が Game を起動するとき、Game は WebSocket サーバーの bind 成功後に
// "READY <port>\n" を標準出力へ書き出す。エディタ側はこの行を継承パイプから読み取り、
// その後でポートへ接続する（boot-order 契約 B1）。
//
// 書き込みは std::cout / printf ではなく WriteFile(GetStdHandle(STD_OUTPUT_HANDLE), ...)
// の生ハンドル経由で行う。CRT は freopen 等で stdout を差し替える可能性があるため、
// 継承パイプへ確実に届けるには生ハンドルを使う必要がある。出力は ASCII のみ。
//
// SDK は要求しない（NorvesLib 内製のヘルパ）。ビルドの一貫性のため Bridge ソースは
// NORVES_BRIDGE_ENABLED でガードされるが、このヘルパ自体は SDK ヘッダに依存しない。
namespace Game::Bridge
{

    /**
     * @brief "READY <port>\n" を STD_OUTPUT_HANDLE の生ハンドルへ書き出してフラッシュする
     *
     * WriteFile を使い、CRT による stdout 差し替えの影響を受けないようにする。
     * ASCII のみを書き出す。
     *
     * @param port 待ち受けポート番号
     * @return 書き込みに成功したら true
     */
    bool WriteReadyLine(uint16_t port);

} // namespace Game::Bridge
