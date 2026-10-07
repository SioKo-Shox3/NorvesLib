# NVMESH v1 の形式識別

NVMESH v1 には、用途の異なる次の二形式があります。同じ主版でもワイヤー構造は互換ではありません。

- [Clustered](NVMESHv1Clustered.md): 材質レコード 128 bytes、クラスタアルゴリズム 1、minor 0。インポート材質を保持する形式
- [LOD graph](NVMESHv1LodGraph.md): 材質レコード 64 bytes、クラスタアルゴリズム 2、minor 0 または 1。LOD 階層・ページを保持する形式

読み込みは minor・材質レコード長・アルゴリズムの組を厳密に識別します。一方の解析に失敗しても他方として再解釈しません。未定義の組は拒否します。

汎用 ModelStaging は Clustered を扱います。LOD graph は階層とページを保持する専用 MegaMesh 変換を使用します。
