// Divides that keep DXIL's fast-math flags (B6): InstCombine's (-X/Y)+Z fold rebuilds the divide, which LLVM 15 left
// without them (dxmt/llvm.sh's CreateFDivFMF fix); the pixel shader's 1/w of SV_Position (DXMT 0064). A precise
// divide stays precise. dxil-translate --flags counts the translated divides that keep flags (fdiv=<flagged>/<all>).
cbuffer C : register(b0) { float4 v; };
float4 psfold() : SV_Target { return (-v.x / v.y) + v.z; }
float4 psprecise() : SV_Target { precise float r = (-v.x / v.y) + v.z; return r; }
float4 pspos(float4 pos : SV_Position) : SV_Target { return pos.w; }
