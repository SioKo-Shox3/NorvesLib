# Resource 基底クラスと読込 API の名前空間

Object/Resource.h の NorvesLib::Core::Resource は参照カウント型の基底クラスとして維持する。
GLTFAnalyzer とモデル読込・ステージング・キャッシュの名前空間は NorvesLib::Core::ResourceIO を使用する。
同じ Core 直下に Resource というクラスと名前空間を宣言すると、同じ翻訳単位に両方のヘッダを含められないため分離した。

## 呼出し側の移行

- NorvesLib::Core::Resource::GLTFAnalyzer は NorvesLib::Core::ResourceIO::GLTFAnalyzer へ変更する
- ModelStaging、モデル読込計画、キャッシュ、非同期キューの修飾名も ResourceIO へ変更する
- include の Resource/ パス、Resource 基底クラス・反射名・継承関係は変更しない
- C++ シンボル名が変わるため、Core と利用側の Game・テスト・外部クライアントを一緒に再ビルドする
- 資産ファイル形式、読込アルゴリズム、描画設定は変更しない

## コンパイル契約

ResourceIONamespaceCompileTest は Resource 基底クラスを先に、GLTFAnalyzer を後に include し、骨格 Resource の継承も検査する。
CookedSkeletalAssetTest は逆順の include を維持する。両者は CookedMeshTest バンドルの別翻訳単位として登録される。
Windows/Core の実コンパイルと実行は未検証。独立した C++ の名前宣言例や参照の静的照合だけでは、この実ビルドを代替しない。
