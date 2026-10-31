// dxil-translate <folder>: translates every .dxil in <folder> with airconv and hands it to Metal (DXIL translator
// plan, Task 5). Compute: a compute pipeline; vertex: a render pipeline with an empty fragment function; pixel: a
// render pipeline with a pass-through vertex function; geometry: a mesh pipeline with a vertex shader from the folder
// whose outputs cover its inputs (geometry shaders are translated last). The root signature comes from the shaders'
// own resources. With --flags, each result line also counts the fast-math flags left in the translated AIR
// (nnan, ninf, fast compares; reassoc, contract, arcp), for checking what the translator keeps, after its memory
// fences (each air.atomic.fence's flags:scope, in order), its air.wg.barrier count and its gradient samples (grad=
// <scaled>/<all>; scaled: an operand is an fmul, as the sampler's LOD bias scaling makes the derivatives), and the
// components its vertex functions output (vout=, position included: varyings at their signature's width, DXMT 0063)
// and its divides that keep any fast-math flag (fdiv=<flagged>/<all>: dxmt/llvm.sh's CreateFDivFMF fix, DXMT 0064). A .dxbc
// file in the folder is a DXBC (Shader Model 5) compute shader, translated as D3D11 does (Task FR: its sync lowering,
// offline). --out <dir> writes each translated metallib to <dir>/<file>.metallib (a shader's later ones, a geometry
// pipeline's, to <file>.<n>.metallib); --use <dir> hands Metal those files' bytes instead of the translator's where they
// exist (DXMTRS Phase 0: AIR rewritten by another LLVM), and air= then hashes the bytes Metal got.
//
// DXMTRS step 4 (the translator's identity checks): --no-metal translates only (no device, no pipelines; ok means every
// compile returned 0) and prints "ok <file> <stage> <ms> air= dis= refl= tags=" or "fail <file> <stage>|- refl=<h>|- <reason>"
// (- when Initialize failed); --initialize-only runs SM50Initialize alone ("init <file> <stage> refl=" or "fail <file> -
// refl=- <reason>"). dis= is the sha256 of the metallibs' AIR as LLVM 15's typed reader prints it, in translation order
// (--dis <dir> writes each as <file>[.<n>].ll); refl= of the 72 reflection bytes, filled with 0xA5 first so a write
// outside the stage's fields shows, and SM50GetArgumentsInfo's two arrays; tags= of each MTLB function's NAME, TYPE,
// VERS, TESS, VATT and VATY tags. --metal 310|320 and --shader-flags <n> set SM50_SHADER_COMMON_DATA (default 310, 0).
// Every translation runs on one 256 KiB thread. The tool needs only airconv_public.h and dxil/dxil_public.h.
//
// DXMTRS step 4, every DXBC stage: a .dxbc vertex, pixel or compute shader is translated standalone with D3D11's argument
// chains (src/d3d11/d3d11_shader.cpp). <name>.hs.dxbc with <name>.vs.dxbc and <name>.ds.dxbc is a tessellation pipeline
// (the vertex-hull function, then the domain one), <name>.gs.dxbc with <name>.vs.dxbc a geometry pipeline (the object
// function, then the mesh one); a group prints on its hull or geometry file's line, its domain file prints none, and a
// missing partner fails with "no <stage> for <name>". Under --no-metal each file or group also runs a fixed variant
// matrix, one "<file>#<v>" line each, with the chain values D3D11 and D3D12 vary: VS #nolayout (stage_in) #f4 #fmt<k>
// (input formats) #so (stream output, rasterization off) #gspt (pass-through GS, rasterization on); PS #d #v; hull
// pipelines #i<f>t<n> (index format, max potential tess factor); geometry pipelines #s0 #s1 (strip topology); every one
// #rs (D3D12's chain: a root signature built from the reflection's slot masks). Variants count in neither the summary
// nor the exit status. With Metal a DXBC compute shader gets its pipeline, a vertex shader a vertex-only one and a
// geometry group a mesh pipeline; a pixel shader and a tessellation group only their libraries and functions (no DXBC
// pass-through vertex exists).
//
// DXMTRS step 4, a capture's pipelines: --pipelines <pipelines.txt> --no-metal [--line <n>] translates the pipelines a
// DXMT_DXIL_DUMP capture lists, one result line per pipelines.txt line named by its 1-based number (--line runs only
// that one, with --dis/--out for a closer look). Shaders are read as <stage>-<hash>.dxil and root signatures as
// rs-<hash>.bin from the file's folder, and every pipeline is compiled with the argument chains, function names and
// root signature D3D12 gives it at creation (cs; vs then ps; or ps, then a geometry pipeline's object and mesh
// functions), the render target formats going through D3D12's DXGI -> Metal mapping. The stage printed is cs or gfx.
#import <Metal/Metal.h>
#include "airconv_public.h"
#include "dxil/dxil_public.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/raw_ostream.h"
#include <CommonCrypto/CommonDigest.h>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <pthread.h>
#include <set>
#include <string>
#include <vector>

static_assert(sizeof(MTL_SHADER_REFLECTION) == 72, "refl= hashes the 72 reflection bytes");

namespace {

std::string ErrorText(sm50_error_t error) {
  char buffer[4096];
  SM50GetErrorMessage(error, buffer, sizeof(buffer));
  SM50FreeError(error);
  return buffer;
}

std::string FirstLine(std::string s) {
  auto end = s.find('\n');
  return end == std::string::npos ? s : s.substr(0, end);
}

struct Sha256 {
  CC_SHA256_CTX ctx;
  Sha256() { CC_SHA256_Init(&ctx); }
  void Add(const void *data, size_t size) { CC_SHA256_Update(&ctx, data, (CC_LONG)size); }
  std::string Hex() {
    unsigned char digest[CC_SHA256_DIGEST_LENGTH];
    CC_SHA256_Final(digest, &ctx);
    char hex[2 * CC_SHA256_DIGEST_LENGTH + 1];
    for (int i = 0; i < CC_SHA256_DIGEST_LENGTH; i++)
      snprintf(hex + 2 * i, 3, "%02x", digest[i]);
    return hex;
  }
};

const char *StageName(uint32_t stage) { // DXIL_SHADER_KIND_* order
  static const char *names[] = {"ps", "vs", "gs", "hs", "ds", "cs"};
  return stage < 6 ? names[stage] : "?";
}

// A DXBC container with an RTS0 (root signature 1.0) part: one descriptor table per resource range, visible to all
// stages, each range at offset 0 of its own table (so an unbounded range can't push another one out of place). Heap
// ranges get none: the flags say both heaps are directly indexed (0xC00), so a shader that indexes one reads it through
// the heap bases after the parameters, as D3D12 binds it (DXIL SM 6.8 plan, Task 9); other shaders gain nothing.
std::vector<uint32_t> RootSignature(const std::vector<DXIL_ENTRY_RESOURCE> &all) {
  const uint32_t header = 6, param = 3, table = 2, range = 5;
  std::vector<DXIL_ENTRY_RESOURCE> resources;
  std::copy_if(all.begin(), all.end(), std::back_inserter(resources), [](const DXIL_ENTRY_RESOURCE &r) { return !r.heap; });
  uint32_t n = resources.size();
  std::vector<uint32_t> rts0{1, n, header * 4, 0, 0, 0xC00};
  for (uint32_t i = 0; i < n; i++) {
    uint32_t payload = (header + param * n + (table + range) * i) * 4;
    rts0.insert(rts0.end(), {0 /* table */, 0 /* all stages */, payload});
  }
  for (uint32_t i = 0; i < n; i++) {
    auto &r = resources[i];
    uint32_t ranges = (header + param * n + (table + range) * i + table) * 4;
    // DXIL_RESOURCE_CLASS_{SRV,UAV,CBUFFER,SAMPLER} are D3D12's range types 0-3.
    rts0.insert(rts0.end(), {1, ranges, r.cls, r.size, r.lower_bound, r.space, 0});
  }
  uint32_t part = rts0.size() * 4, total = 36 + 8 + part;
  std::vector<uint32_t> container{0x43425844 /* DXBC */, 0, 0, 0, 0, 1, total, 1, 36, 0x30535452 /* RTS0 */, part};
  container.insert(container.end(), rts0.begin(), rts0.end());
  return container;
}

struct Flags {
  unsigned nnan = 0, ninf = 0, cmp = 0, reassoc = 0, contract = 0, arcp = 0, barriers = 0, grads = 0, scaled_grads = 0, vout = 0, fdivs = 0, fast_fdivs = 0;
  std::string fences; // "flags:scope" of each air.atomic.fence, comma-separated
};

struct Result {
  bool ok = false, deferred = false;
  const char *stage = "-"; // known once Initialize succeeds
  std::string reason, refl = "-";
  Flags flags; // --flags: summed over the pipeline's translated functions
  uint64_t air = 0xcbf29ce484222325; // FNV-1a 64 of every translated metallib's bytes, in translation order
  Sha256 dis, tags;                   // over every translated metallib, in translation order
  std::string suffix;                 // a variant's: its line is <file>#<suffix>
  double ms = 0;                      // a variant's translation time
  std::vector<Result> variants;       // --no-metal, DXBC: the matrix, printed after this result's line
};

bool count_flags = false, no_metal = false, init_only = false;
std::filesystem::path out_dir, use_dir, dis_dir;
const char *folder = nullptr, *pipelines_file = nullptr;
uint32_t only_line = 0; // --pipelines --line <n>
SM50_SHADER_METAL_VERSION metal_version = SM50_SHADER_METAL_310;
uint32_t shader_flags = 0;
std::string metallib_name; // the current shader's file name, and how many metallibs it has translated so far
unsigned metallib_count = 0;
double tool_ms = 0; // the current shader's time in dis= and tags=, which isn't translation

SM50_SHADER_COMMON_DATA Common() {
  return {nullptr, SM50_SHADER_COMMON, metal_version, (SM50_SHADER_FLAG)shader_flags};
}

// A translated metallib's AIR, read by LLVM 15's typed reader (its bitcode starts at the wrapper magic), with an empty
// module identifier.
llvm::Expected<std::unique_ptr<llvm::Module>> ParseAIR(const void *data, size_t size, llvm::LLVMContext &context) {
  std::string_view bytes((const char *)data, size);
  auto at = bytes.find(std::string_view("\xde\xc0\x17\x0b", 4));
  if (at == std::string_view::npos)
    return llvm::createStringError(llvm::inconvertibleErrorCode(), "no bitcode in the metallib");
  context.setOpaquePointers(false);
  return llvm::parseBitcodeFile(llvm::MemoryBufferRef(llvm::StringRef(bytes.data() + at, size - at), ""), context);
}

// Counts the fast-math flags of a translated metallib's AIR.
void CountFlags(const void *data, size_t size, Flags &flags) {
  llvm::LLVMContext context;
  auto module = ParseAIR(data, size, context);
  if (!module) {
    llvm::consumeError(module.takeError());
    return;
  }
  for (auto &function : **module)
    for (auto &block : function)
      for (auto &inst : block) {
        if (auto call = llvm::dyn_cast<llvm::CallInst>(&inst); call && call->getCalledFunction()) {
          auto name = call->getCalledFunction()->getName();
          auto arg = [&](unsigned i) {
            auto c = llvm::dyn_cast<llvm::ConstantInt>(call->getArgOperand(i));
            return c ? std::to_string(c->getZExtValue()) : std::string("?");
          };
          if (name == "air.atomic.fence")
            flags.fences += (flags.fences.empty() ? "" : ",") + arg(0) + ":" + arg(2);
          flags.barriers += name == "air.wg.barrier";
          if (name.startswith("air.sample_texture") && name.contains("_grad")) {
            flags.grads++;
            flags.scaled_grads += std::any_of(call->arg_begin(), call->arg_end(), [](auto &a) {
              auto op = llvm::dyn_cast<llvm::BinaryOperator>(a.get());
              return op && op->getOpcode() == llvm::Instruction::FMul;
            });
          }
        }
        if (llvm::isa<llvm::FPMathOperator>(&inst)) {
          auto f = inst.getFastMathFlags();
          flags.nnan += f.noNaNs();
          flags.ninf += f.noInfs();
          flags.cmp += llvm::isa<llvm::FCmpInst>(&inst) && f.any();
          flags.reassoc += f.allowReassoc();
          flags.contract += f.allowContract();
          flags.arcp += f.allowReciprocal();
          if (inst.getOpcode() == llvm::Instruction::FDiv) {
            flags.fdivs++;
            flags.fast_fdivs += f.any();
          }
        }
      }
  if (auto vertex = (*module)->getNamedMetadata("air.vertex"))
    for (auto *node : vertex->operands())
      if (auto *f = llvm::mdconst::dyn_extract_or_null<llvm::Function>(node->getOperand(0)))
        if (auto *ret = llvm::dyn_cast<llvm::StructType>(f->getReturnType()))
          for (auto *field : ret->elements())
            flags.vout += llvm::isa<llvm::FixedVectorType>(field) ? llvm::cast<llvm::FixedVectorType>(field)->getNumElements() : 1;
}

// tags=: the MTLB function list as airconv's metallib_writer.cpp writes it (an 88-byte header; at FunctionListOffset the
// function count, the list's size, then each function's tags up to ENDT, a tag being a fourcc, a u16 size and its
// bytes; at PublicMetadataOffset plus the function's OFFT offset a u32 size and its tags up to ENDT). Each function's
// NAME, TYPE, VERS and TESS tags, then its public VATT and VATY ones, in order, as fourcc and bytes; HASH, OFFT, MDSZ
// and every size left out. A metallib this can't walk adds "malformed" where it stopped.
void Tags(const uint8_t *p, size_t size, Sha256 &h) {
  auto has = [&](uint64_t at, uint64_t n) { return at <= size && n <= size - at; };
  auto is = [&](uint64_t at, const char *tag) { return !memcmp(p + at, tag, 4); };
  auto u64 = [&](uint64_t at) {
    uint64_t v;
    memcpy(&v, p + at, 8);
    return v;
  };
  // The tags from `at` to ENDT: hashes those in `keep`, reads OFFT's public metadata offset; returns where the next
  // record starts, or 0 when a tag runs past the end.
  auto walk = [&](uint64_t at, std::initializer_list<const char *> keep, uint64_t &public_offset) -> uint64_t {
    while (has(at, 4) && !is(at, "ENDT")) {
      uint16_t n = 0;
      if (!has(at, 6))
        return 0;
      memcpy(&n, p + at + 4, 2);
      if (!has(at + 6, n))
        return 0;
      if (std::any_of(keep.begin(), keep.end(), [&](const char *tag) { return is(at, tag); })) {
        h.Add(p + at, 4);
        h.Add(p + at + 6, n);
      }
      if (is(at, "OFFT") && n >= 8)
        public_offset = u64(at + 6);
      at += 6 + n;
    }
    return has(at, 4) ? at + 4 : 0;
  };
  uint32_t count = 0;
  uint64_t at = has(0, 88) && is(0, "MTLB") && has(u64(24), 8) ? u64(24) : 0; // FunctionListOffset
  if (at) {
    memcpy(&count, p + at, 4);
    at += 8; // the count and the list's size
  }
  for (uint32_t i = 0; at && i < count; i++) {
    uint64_t offset = ~0ull, unused;
    at = walk(at, {"NAME", "TYPE", "VERS", "TESS"}, offset);
    uint64_t metadata = u64(40) + offset; // PublicMetadataOffset: the function's u32 size, then its tags
    if (offset == ~0ull || metadata < offset || !has(metadata, 4) || !walk(metadata + 4, {"VATT", "VATY"}, unused))
      at = 0;
  }
  if (!at)
    h.Add("malformed", 9);
}

// dis= and --dis: the AIR's text, as LLVM 15's typed reader prints it; tags=.
void Describe(const void *data, size_t size, const std::string &file, Result &result) {
  auto start = std::chrono::steady_clock::now();
  std::string text;
  llvm::raw_string_ostream os(text);
  llvm::LLVMContext context;
  if (auto module = ParseAIR(data, size, context))
    (*module)->print(os, nullptr);
  else
    os << "; dxil-translate: " << llvm::toString(module.takeError()) << "\n";
  os.flush();
  result.dis.Add(text.data(), text.size());
  if (!dis_dir.empty())
    std::ofstream(dis_dir / (file + ".ll"), std::ios::binary).write(text.data(), text.size());
  Tags((const uint8_t *)data, size, result.tags);
  tool_ms += std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

// refl=: the reflection SM50Initialize wrote over 0xA5 bytes, then SM50GetArgumentsInfo's arrays at its counts.
std::string ReflectionHash(sm50_shader_t shader, const MTL_SHADER_REFLECTION &refl) {
  Sha256 h;
  h.Add(&refl, sizeof(refl));
  // ponytail: counts past D3D11's binding slots (~222) mean the counts weren't written; hashed as that, not allocated.
  if (refl.NumConstantBuffers > 4096 || refl.NumArguments > 4096) {
    h.Add("counts out of range", 19);
    return h.Hex();
  }
  std::vector<MTL_SM50_SHADER_ARGUMENT> cbs(refl.NumConstantBuffers), args(refl.NumArguments);
  memset(cbs.data(), 0xA5, cbs.size() * sizeof(cbs[0]));
  memset(args.data(), 0xA5, args.size() * sizeof(args[0]));
  SM50GetArgumentsInfo(shader, cbs.data(), args.data());
  h.Add(cbs.data(), cbs.size() * sizeof(cbs[0]));
  h.Add(args.data(), args.size() * sizeof(args[0]));
  return h.Hex();
}

// A vertex shader seen in the folder, for pairing with geometry shaders: its bytecode and output semantics.
struct VertexShader {
  std::vector<char> bytes;
  std::set<std::string> outputs;
};

// Varyings link by semantic, not register: "TEXCOORD0" (as airconv's dxil_types.hpp UserName).
std::string Semantic(const DXIL_ENTRY_ELEMENT &e, uint32_t row) {
  if (e.kind == DXIL_SEMANTIC_POSITION)
    return "SV_POSITION";
  std::string name = e.name;
  for (auto &c : name)
    c = (char)toupper((unsigned char)c);
  return name + std::to_string(row < e.num_semantic_indices ? e.semantic_indices[row] : row);
}

// The MTLAttributeFormats airconv's vertex pulling converts (air_operations.cpp, pull_vec4_from_addr_checked: every format
// but Invalid and Int1010102Normalized, whose case asserts "unused"), in the enum's order: the pool of #fmt<k>.
const MTLAttributeFormat kIaFormats[] = {
    MTLAttributeFormatUChar2,           MTLAttributeFormatUChar4,           MTLAttributeFormatChar2,
    MTLAttributeFormatChar4,            MTLAttributeFormatUChar2Normalized, MTLAttributeFormatUChar4Normalized,
    MTLAttributeFormatChar2Normalized,  MTLAttributeFormatChar4Normalized,  MTLAttributeFormatUShort2,
    MTLAttributeFormatUShort4,          MTLAttributeFormatShort2,           MTLAttributeFormatShort4,
    MTLAttributeFormatUShort2Normalized, MTLAttributeFormatUShort4Normalized, MTLAttributeFormatShort2Normalized,
    MTLAttributeFormatShort4Normalized, MTLAttributeFormatHalf2,            MTLAttributeFormatHalf4,
    MTLAttributeFormatFloat,            MTLAttributeFormatFloat2,           MTLAttributeFormatFloat3,
    MTLAttributeFormatFloat4,           MTLAttributeFormatInt,              MTLAttributeFormatInt2,
    MTLAttributeFormatInt3,             MTLAttributeFormatInt4,             MTLAttributeFormatUInt,
    MTLAttributeFormatUInt2,            MTLAttributeFormatUInt3,            MTLAttributeFormatUInt4,
    MTLAttributeFormatUInt1010102Normalized, MTLAttributeFormatUChar4Normalized_BGRA, MTLAttributeFormatUChar,
    MTLAttributeFormatChar,             MTLAttributeFormatUCharNormalized,  MTLAttributeFormatCharNormalized,
    MTLAttributeFormatUShort,           MTLAttributeFormatShort,            MTLAttributeFormatUShortNormalized,
    MTLAttributeFormatShortNormalized,  MTLAttributeFormatHalf,             MTLAttributeFormatFloatRG11B10,
    MTLAttributeFormatFloatRGB9E5,
};
static_assert(std::size(kIaFormats) == 43, "pull_vec4_from_addr_checked converts 43 formats");

// One input layout element per vertex shader input register, slot 0: R32G32B32A32_FLOAT, or, with `formats` k >= 0
// (#fmt<k>), the i-th of n elements takes kIaFormats[(i + k * n) mod 43].
std::vector<SM50_IA_INPUT_ELEMENT> InputLayout(const DXIL_ENTRY_INFO &vs, int formats = -1) {
  std::vector<SM50_IA_INPUT_ELEMENT> elements;
  for (uint32_t i = 0; i < vs.num_inputs; i++) {
    auto &e = vs.inputs[i];
    if (e.kind == DXIL_SEMANTIC_ARBITRARY)
      for (uint32_t r = 0; r < e.rows; r++)
        elements.push_back({uint32_t(e.start_row) + r, 0, 16 * (uint32_t(e.start_row) + r), MTLAttributeFormatFloat4, 0, 0});
  }
  for (size_t i = 0; formats >= 0 && i < elements.size(); i++)
    elements[i].format = kIaFormats[(i + size_t(formats) * elements.size()) % std::size(kIaFormats)];
  return elements;
}

std::vector<DXIL_ENTRY_RESOURCE> Resources(const DXIL_ENTRY_INFO &entry) {
  return std::vector<DXIL_ENTRY_RESOURCE>(entry.resources, entry.resources + entry.num_resources);
}

// Records a translated metallib (--out, --use, --flags, air=, dis=, --dis, tags=) and, with a device, loads it.
id<MTLLibrary> Library(id<MTLDevice> device, sm50_bitcode_t bitcode, Result &result) {
  SM50_COMPILED_BITCODE data;
  SM50GetCompiledBitcode(bitcode, &data);
  auto file = metallib_name + (metallib_count ? "." + std::to_string(metallib_count) : "");
  metallib_count++;
  if (!out_dir.empty())
    std::ofstream(out_dir / (file + ".metallib"), std::ios::binary).write((const char *)data.Data, data.Size);
  std::vector<char> used;
  if (!use_dir.empty() && std::filesystem::exists(use_dir / (file + ".metallib"))) {
    std::ifstream in(use_dir / (file + ".metallib"), std::ios::binary);
    used.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    data.Data = (sm50_ptr64_t)used.data();
    data.Size = used.size();
  }
  if (count_flags)
    CountFlags(data.Data, data.Size, result.flags);
  for (size_t i = 0; i < data.Size; i++)
    result.air = (result.air ^ ((const uint8_t *)data.Data)[i]) * 0x100000001b3;
  if (no_metal || !dis_dir.empty())
    Describe(data.Data, data.Size, file, result);
  if (!device) {
    SM50DestroyBitcode(bitcode);
    return nil;
  }
  // Copied (the default destructor): the bytes may be `used`'s, which go with this function.
  auto dispatch = dispatch_data_create((const void *)data.Data, data.Size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  SM50DestroyBitcode(bitcode);
  NSError *err = nil;
  id<MTLLibrary> lib = [device newLibraryWithData:dispatch error:&err];
  if (!lib)
    result.reason = FirstLine(err.localizedDescription.UTF8String);
  return lib;
}

// A geometry shader's mesh pipeline from its two libraries (ok without a device once both exist).
void MeshPipeline(id<MTLDevice> device, id<MTLLibrary> object_lib, id<MTLLibrary> mesh_lib, NSString *object_name,
                  NSString *mesh_name, Result &result) {
  if (!device) {
    result.ok = true;
    return;
  }
  auto desc = [MTLMeshRenderPipelineDescriptor new];
  desc.objectFunction = [object_lib newFunctionWithName:object_name];
  desc.meshFunction = [mesh_lib newFunctionWithName:mesh_name];
  if (!desc.objectFunction || !desc.meshFunction) {
    if (result.reason.empty())
      result.reason = "no object or mesh function in the library";
    return;
  }
  desc.payloadMemoryLength = 16256; // as airconv's geometry pipeline and DXMT's D3D11 declare it
  desc.rasterizationEnabled = NO;
  NSError *err = nil;
  if (![device newRenderPipelineStateWithMeshDescriptor:desc options:MTLPipelineOptionNone reflection:nil error:&err]) {
    result.reason = FirstLine(err ? err.localizedDescription.UTF8String : "no pipeline");
    return;
  }
  result.ok = true;
}

// A geometry shader's mesh pipeline: `vs` (a folder vertex shader covering its inputs) as the object function.
void TranslateGeometry(id<MTLDevice> device, sm50_shader_t gs, const DXIL_ENTRY_INFO &entry,
                       const std::vector<VertexShader> &vertex_shaders, bool last_chance, Result &result) {
  std::set<std::string> needs;
  for (uint32_t i = 0; i < entry.num_inputs; i++) {
    auto &e = entry.inputs[i];
    if (e.kind == DXIL_SEMANTIC_ARBITRARY || e.kind == DXIL_SEMANTIC_POSITION)
      for (uint32_t r = 0; r < e.rows; r++)
        needs.insert(Semantic(e, r));
  }
  const VertexShader *partner = nullptr;
  for (auto &v : vertex_shaders)
    if (std::includes(v.outputs.begin(), v.outputs.end(), needs.begin(), needs.end())) {
      partner = &v;
      break;
    }
  if (!partner) {
    result.deferred = !last_chance;
    result.reason = "no vertex shader in the folder writes its inputs";
    return;
  }
  sm50_shader_t vs = nullptr;
  sm50_error_t error = nullptr;
  MTL_SHADER_REFLECTION refl{};
  if (SM50Initialize(partner->bytes.data(), partner->bytes.size(), &vs, &refl, &error)) {
    result.reason = FirstLine(ErrorText(error));
    return;
  }
  DXIL_ENTRY_INFO vs_entry;
  DXILGetEntry(vs, &vs_entry);
  auto resources = Resources(vs_entry), gs_resources = Resources(entry);
  resources.insert(resources.end(), gs_resources.begin(), gs_resources.end());
  auto rootsig_blob = RootSignature(resources);
  SM50_SHADER_COMMON_DATA common = Common();
  SM50_SHADER_PSO_GEOMETRY_SHADER_DATA list{&common, SM50_SHADER_PSO_GEOMETRY_SHADER, false};
  auto elements = InputLayout(vs_entry);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&list, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE,
                                      elements.empty() ? 0u : 1u, (uint32_t)elements.size(), elements.data()};
  SM50_SHADER_ROOT_SIGNATURE_DATA object_args{&ia, SM50_SHADER_ROOT_SIGNATURE, rootsig_blob.data(), rootsig_blob.size() * 4};
  SM50_SHADER_ROOT_SIGNATURE_DATA mesh_args{&list, SM50_SHADER_ROOT_SIGNATURE, rootsig_blob.data(), rootsig_blob.size() * 4};
  sm50_bitcode_t object_bitcode = nullptr, mesh_bitcode = nullptr;
  if (SM50CompileGeometryPipelineVertex(vs, gs, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&object_args, "vs_object",
                                        &object_bitcode, &error) ||
      SM50CompileGeometryPipelineGeometry(vs, gs, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&mesh_args, "gs_mesh",
                                          &mesh_bitcode, &error)) {
    result.reason = FirstLine(ErrorText(error));
    if (object_bitcode)
      SM50DestroyBitcode(object_bitcode);
    SM50Destroy(vs);
    return;
  }
  SM50Destroy(vs);
  auto object_lib = Library(device, object_bitcode, result);
  auto mesh_lib = Library(device, mesh_bitcode, result);
  MeshPipeline(device, object_lib, mesh_lib, @"vs_object", @"gs_mesh", result);
}

// A shader of the folder, initialized: its handle, entry, reflection and refl= hash (`error` says why not).
struct Shader {
  sm50_shader_t handle = nullptr;
  DXIL_ENTRY_INFO entry{};
  MTL_SHADER_REFLECTION refl;
  std::string hash = "-", error;
  Shader() = default;
  Shader(const Shader &) = delete;
  ~Shader() {
    if (handle)
      SM50Destroy(handle);
  }
  bool Init(const std::vector<char> &bytes) {
    memset(&refl, 0xA5, sizeof(refl)); // only the stage's fields may be written (DXMTRS step 4, Review Focus 3)
    sm50_error_t e = nullptr;
    if (SM50Initialize(bytes.data(), bytes.size(), &handle, &refl, &e)) {
      handle = nullptr;
      error = FirstLine(ErrorText(e));
      return false;
    }
    DXILGetEntry(handle, &entry);
    hash = ReflectionHash(handle, refl);
    return true;
  }
};

// Metal rejects a non-void vertex function with rasterization off, so a vertex-only pipeline gets an empty fragment
// function.
id<MTLFunction> EmptyFragment(id<MTLDevice> device) {
  static id<MTLFunction> empty = [[device newLibraryWithSource:@"fragment void ps_empty() {}" options:nil error:nil]
      newFunctionWithName:@"ps_empty"];
  return empty;
}

// The argument-chain values a variant changes; the defaults are the base translation (D3D11's chain).
struct Chain {
  bool layout = true;                                                    // an IA layout; false: stage_in (#nolayout)
  int formats = -1;                                                      // -1: every input Float4; k: #fmt<k>
  bool so = false;                                                       // vertex stream output (#so)
  bool gspt = false;                                                     // a pass-through GS: an RTAI output (#gspt)
  uint32_t sample_mask = 0xffffffff, unorm_mask = 0;                     // pixel (#d, #v)
  bool dual = false, no_depth = false;                                   // pixel
  SM50_INDEX_BUFFER_FORMAT index_format = SM50_INDEX_BUFFER_FORMAT_NONE; // hull
  uint32_t tess_factor = 0;                                              // hull: 0 is the domain shader's own
  bool strip = false;                                                    // geometry (#s1)
  const std::vector<uint32_t> *rootsig = nullptr;                        // #rs: D3D12's chain
};

SM50_SHADER_ROOT_SIGNATURE_DATA RootNode(const Chain &c, SM50_SHADER_COMPILATION_ARGUMENT_TYPE type, void *next) {
  return {next, type, c.rootsig ? c.rootsig->data() : nullptr, c.rootsig ? c.rootsig->size() * 4 : 0};
}

// #rs: the root signature D3D12 builds for DXBC shaders, here one single-slot range per bit of the shaders' reflection
// slot masks, space 0 (SRV slots 64 and up are SRVSlotMaskHi's).
std::vector<uint32_t> SlotsRootSignature(std::initializer_list<const MTL_SHADER_REFLECTION *> reflections) {
  std::set<std::pair<uint32_t, uint32_t>> slots; // class, slot
  for (auto *refl : reflections) {
    auto add = [&](uint32_t cls, uint64_t mask, uint32_t base) {
      for (uint32_t bit = 0; bit < 64; bit++)
        if (mask >> bit & 1)
          slots.insert({cls, base + bit});
    };
    add(DXIL_RESOURCE_CLASS_SRV, refl->SRVSlotMaskLo, 0);
    add(DXIL_RESOURCE_CLASS_SRV, refl->SRVSlotMaskHi, 64);
    add(DXIL_RESOURCE_CLASS_UAV, refl->UAVSlotMask, 0);
    add(DXIL_RESOURCE_CLASS_CBUFFER, refl->ConstantBufferSlotMask, 0);
    add(DXIL_RESOURCE_CLASS_SAMPLER, refl->SamplerSlotMask, 0);
  }
  std::vector<DXIL_ENTRY_RESOURCE> resources;
  for (auto [cls, slot] : slots)
    resources.push_back({cls, 0, slot, 1, 0});
  return RootSignature(resources);
}

// --no-metal: one more line, <file>#<tag>, for the same shader or group under other chain values. `body` translates
// into the new result. Its ms leaves out its dis= and tags= work, and the base line leaves out all of it (tool_ms).
template <class Body> void Variant(Result &base, const std::string &tag, Body &&body) {
  Result r;
  r.stage = base.stage, r.refl = base.refl, r.suffix = tag;
  std::string file = metallib_name;
  metallib_name = file + "#" + tag, metallib_count = 0;
  double outer = tool_ms;
  auto start = std::chrono::steady_clock::now();
  body(r);
  double elapsed = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
  r.ms = elapsed - (tool_ms - outer);
  tool_ms = outer + elapsed;
  metallib_name = file;
  base.variants.push_back(std::move(r));
}

// One SM50Compile ("main") of a DXBC compute, vertex or pixel shader with D3D11's chain for it (src/d3d11/
// d3d11_shader.cpp), or with D3D12's when `c.rootsig` is set; null with r.reason on a refusal.
sm50_bitcode_t CompileOne(Shader &s, const Chain &c, Result &r) {
  bool vertex = s.entry.stage == DXIL_SHADER_KIND_VERTEX, pixel = s.entry.stage == DXIL_SHADER_KIND_PIXEL;
  SM50_SHADER_COMMON_DATA common = Common();
  std::vector<SM50_IA_INPUT_ELEMENT> elements;
  if (vertex)
    elements = InputLayout(s.entry, c.formats);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&common, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE,
                                      elements.empty() ? 0u : 1u, (uint32_t)elements.size(), elements.data()};
  void *next = vertex && c.layout ? (void *)&ia : (void *)&common;
  // D3D11's vertex chain: the pass-through GS node (none: every register 255) -> IA layout -> common; stream output
  // takes the first node's place.
  SM50_SHADER_GS_PASS_THROUGH_DATA pt{};
  pt.next = next, pt.type = SM50_SHADER_GS_PASS_THROUGH, pt.DataEncoded = ~0u;
  if (c.gspt)
    pt.Data = {0, 0, 255, 255}; // the render target array index in o0.x, no viewport index; rasterization stays on
  std::vector<SM50_STREAM_OUTPUT_ELEMENT> so_elements;
  SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT_DATA so{};
  so.next = next, so.type = SM50_SHADER_EMULATE_VERTEX_STREAM_OUTPUT;
  if (c.so) { // every output register's four components, in buffer 0
    std::set<int32_t> registers;
    for (uint32_t i = 0; i < s.entry.num_outputs; i++)
      if (s.entry.outputs[i].start_row >= 0)
        registers.insert(s.entry.outputs[i].start_row);
    uint32_t offset = 0;
    for (auto reg : registers)
      for (uint32_t component = 0; component < 4; component++, offset += 4)
        so_elements.push_back({uint32_t(reg), component, 0, offset});
    so.num_elements = (uint32_t)so_elements.size(), so.strides[0] = offset, so.elements = so_elements.data();
  }
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pso{&common, SM50_SHADER_PSO_PIXEL_SHADER, c.sample_mask, c.dual, c.no_depth,
                                        c.unorm_mask, {}};
  for (uint32_t i = 0; pixel && i < 8; i++)
    if (s.refl.PixelShader.ValidRenderTargets & (1 << i))
      pso.pixel_formats[i] = MTLPixelFormatRGBA8Unorm;
  void *chain = vertex ? (c.so ? (void *)&so : (void *)&pt) : pixel ? (void *)&pso : (void *)&common;
  auto rootsig = RootNode(c, SM50_SHADER_ROOT_SIGNATURE, vertex ? next : chain);
  sm50_bitcode_t bitcode = nullptr;
  sm50_error_t error = nullptr;
  if (SM50Compile(s.handle, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)(c.rootsig ? (void *)&rootsig : chain), "main",
                  &bitcode, &error)) {
    r.reason = FirstLine(ErrorText(error));
    return nullptr;
  }
  return bitcode;
}

// A DXBC compute, vertex or pixel shader with the chain `c`; r.ok once Metal (if any) takes it: a compute pipeline, a
// vertex-only pipeline, or for a pixel shader (no DXBC pass-through vertex exists) the library and its function.
void Standalone(id<MTLDevice> device, Shader &s, const Chain &c, Result &r) {
  sm50_bitcode_t bitcode = CompileOne(s, c, r);
  if (!bitcode)
    return;
  id<MTLLibrary> lib = Library(device, bitcode, r);
  if (!device) {
    r.ok = true;
    return;
  }
  id<MTLFunction> function = [lib newFunctionWithName:@"main"];
  if (!function) {
    if (r.reason.empty())
      r.reason = "no function \"main\" in the library";
    return;
  }
  if (s.entry.stage == DXIL_SHADER_KIND_PIXEL) {
    r.ok = true;
    return;
  }
  NSError *err = nil;
  id pipeline = nil;
  if (s.entry.stage == DXIL_SHADER_KIND_COMPUTE) {
    pipeline = [device newComputePipelineStateWithFunction:function error:&err];
  } else {
    auto desc = [MTLRenderPipelineDescriptor new];
    desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle;
    desc.vertexFunction = function;
    desc.fragmentFunction = EmptyFragment(device);
    pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&err];
  }
  r.ok = pipeline != nil;
  if (!r.ok)
    r.reason = FirstLine(err ? err.localizedDescription.UTF8String : "no pipeline");
}

// The variant matrix of a standalone DXBC shader (--no-metal).
void StandaloneVariants(Shader &s, Result &result) {
  auto vary = [&](const std::string &tag, const Chain &c) {
    Variant(result, tag, [&](Result &r) { Standalone(nullptr, s, c, r); });
  };
  Chain c;
  if (s.entry.stage == DXIL_SHADER_KIND_VERTEX) {
    c.layout = false;
    vary("nolayout", c);
    vary("f4", Chain{});
    // Input i takes format (i + k * n) mod 43, k until each format has appeared once.
    size_t n = InputLayout(s.entry).size();
    for (size_t k = 0; n && k * n < std::size(kIaFormats); k++) {
      c = Chain{};
      c.formats = (int)k;
      vary("fmt" + std::to_string(k), c);
    }
    c = Chain{};
    c.so = true;
    vary("so", c);
    c = Chain{};
    c.gspt = true;
    vary("gspt", c);
  } else if (s.entry.stage == DXIL_SHADER_KIND_PIXEL) {
    vary("d", Chain{});
    c.sample_mask = 0x1, c.dual = true, c.no_depth = true, c.unorm_mask = 0xff;
    vary("v", c);
  }
  auto rootsig = SlotsRootSignature({&s.refl});
  c = Chain{};
  c.rootsig = &rootsig;
  vary("rs", c);
}

// A tessellation pipeline as D3D11 compiles it (d3d11_shader.cpp): the vertex-hull function, then the domain one.
// r.ok when both compiled (and, with Metal, both libraries have their functions).
void HullPipeline(id<MTLDevice> device, Shader &vs, Shader &hs, Shader &ds, const Chain &c, Result &r) {
  SM50_SHADER_COMMON_DATA common = Common();
  SM50_SHADER_PSO_TESSELLATOR_DATA tess{&common, SM50_SHADER_PSO_TESSELLATOR,
                                        c.tess_factor ? c.tess_factor : ds.refl.PostTessellator.MaxPotentialTessFactor};
  auto elements = InputLayout(vs.entry);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&tess, SM50_SHADER_IA_INPUT_LAYOUT, c.index_format, elements.empty() ? 0u : 1u,
                                      (uint32_t)elements.size(), elements.data()};
  SM50_SHADER_GS_PASS_THROUGH_DATA pt{};
  pt.next = &tess, pt.type = SM50_SHADER_GS_PASS_THROUGH, pt.DataEncoded = ~0u;
  auto vertex_part = RootNode(c, SM50_SHADER_ROOT_SIGNATURE2, &ia);
  auto hull_rootsig = RootNode(c, SM50_SHADER_ROOT_SIGNATURE, &vertex_part);
  auto domain_rootsig = RootNode(c, SM50_SHADER_ROOT_SIGNATURE, &pt);
  sm50_bitcode_t hull = nullptr, domain = nullptr;
  sm50_error_t error = nullptr;
  if (SM50CompileTessellationPipelineHull(
          vs.handle, hs.handle, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)(c.rootsig ? (void *)&hull_rootsig : (void *)&ia),
          "vshs_main", &hull, &error)) {
    r.reason = FirstLine(ErrorText(error));
    return;
  }
  auto hull_lib = Library(device, hull, r);
  if (SM50CompileTessellationPipelineDomain(
          hs.handle, ds.handle, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)(c.rootsig ? (void *)&domain_rootsig : (void *)&pt),
          "ds_main", &domain, &error)) {
    r.reason = FirstLine(ErrorText(error));
    return;
  }
  auto domain_lib = Library(device, domain, r);
  if (device && (![hull_lib newFunctionWithName:@"vshs_main"] || ![domain_lib newFunctionWithName:@"ds_main"])) {
    if (r.reason.empty())
      r.reason = "no vertex-hull or domain function in the library";
    return;
  }
  r.ok = true;
}

// A geometry pipeline as D3D11 compiles it: the object function (the vertex shader), then the mesh one.
void GeometryPipeline(id<MTLDevice> device, Shader &vs, Shader &gs, const Chain &c, Result &r) {
  SM50_SHADER_COMMON_DATA common = Common();
  auto elements = InputLayout(vs.entry);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&common, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE,
                                      elements.empty() ? 0u : 1u, (uint32_t)elements.size(), elements.data()};
  SM50_SHADER_PSO_GEOMETRY_SHADER_DATA object_pso{&ia, SM50_SHADER_PSO_GEOMETRY_SHADER, c.strip},
      mesh_pso{&common, SM50_SHADER_PSO_GEOMETRY_SHADER, c.strip};
  auto object_part = RootNode(c, SM50_SHADER_ROOT_SIGNATURE2, &object_pso); // the C++ geometry converter ignores this one
  auto object_rootsig = RootNode(c, SM50_SHADER_ROOT_SIGNATURE, &object_part);
  auto mesh_rootsig = RootNode(c, SM50_SHADER_ROOT_SIGNATURE, &mesh_pso);
  sm50_bitcode_t object = nullptr, mesh = nullptr;
  sm50_error_t error = nullptr;
  if (SM50CompileGeometryPipelineVertex(
          vs.handle, gs.handle,
          (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)(c.rootsig ? (void *)&object_rootsig : (void *)&object_pso),
          "vsgs_main", &object, &error)) {
    r.reason = FirstLine(ErrorText(error));
    return;
  }
  auto object_lib = Library(device, object, r);
  if (SM50CompileGeometryPipelineGeometry(
          vs.handle, gs.handle,
          (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)(c.rootsig ? (void *)&mesh_rootsig : (void *)&mesh_pso), "gs_main",
          &mesh, &error)) {
    r.reason = FirstLine(ErrorText(error));
    return;
  }
  auto mesh_lib = Library(device, mesh, r);
  MeshPipeline(device, object_lib, mesh_lib, @"vsgs_main", @"gs_main", r);
}

bool EndsWith(const std::string &s, const std::string &suffix) {
  return s.size() >= suffix.size() && !s.compare(s.size() - suffix.size(), suffix.size(), suffix);
}

// <name> of <name>.<stage>.dxbc (or of <name>.dxbc): the files of a pipeline group share it.
std::string Stem(const std::string &file) {
  for (auto *suffix : {".vs.dxbc", ".hs.dxbc", ".ds.dxbc", ".gs.dxbc", ".ps.dxbc", ".cs.dxbc", ".dxbc"})
    if (EndsWith(file, suffix))
      return file.substr(0, file.size() - strlen(suffix));
  return file;
}

// A pipeline group's <stem>.<stage>.dxbc, read and initialized; false with `error` when missing, refused or another stage.
bool Member(const std::string &stem, uint32_t kind, Shader &s, std::string &error) {
  std::string name = stem + "." + StageName(kind) + ".dxbc";
  std::ifstream in(std::filesystem::path(folder) / name, std::ios::binary);
  if (!in) {
    error = std::string("no ") + StageName(kind) + " for " + stem;
    return false;
  }
  if (!s.Init(std::vector<char>((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()))) {
    error = name + ": " + s.error;
    return false;
  }
  if (s.entry.stage != kind) {
    error = name + " is not a " + StageName(kind) + " shader";
    return false;
  }
  return true;
}

// <name>.hs.dxbc (with <name>.vs.dxbc and <name>.ds.dxbc) or <name>.gs.dxbc (with <name>.vs.dxbc): the group's
// pipeline, printed on this file's line (refl= is this file's own; the others have their own lines).
Result TranslatePipeline(id<MTLDevice> device, const std::string &file, const std::vector<char> &bytes) {
  Result result;
  bool geometry = EndsWith(file, ".gs.dxbc");
  uint32_t kind = geometry ? DXIL_SHADER_KIND_GEOMETRY : DXIL_SHADER_KIND_HULL;
  Shader primary, vs, ds;
  std::string error;
  if (!primary.Init(bytes)) {
    result.reason = primary.error;
    return result;
  }
  result.stage = StageName(primary.entry.stage);
  result.refl = primary.hash;
  if (primary.entry.stage != kind) {
    result.reason = std::string("not a ") + StageName(kind) + " shader";
    return result;
  }
  std::string stem = Stem(file);
  if (!Member(stem, DXIL_SHADER_KIND_VERTEX, vs, error) ||
      (!geometry && !Member(stem, DXIL_SHADER_KIND_DOMAIN, ds, error))) {
    result.reason = error;
    return result;
  }
  auto run = [&](Result &r, const Chain &c) {
    if (geometry)
      GeometryPipeline(device, vs, primary, c, r);
    else
      HullPipeline(device, vs, primary, ds, c, r);
  };
  run(result, Chain{});
  if (!no_metal)
    return result;
  auto vary = [&](const std::string &tag, const Chain &c) {
    Variant(result, tag, [&](Result &r) { run(r, c); });
  };
  Chain c;
  if (geometry) {
    vary("s0", c);
    c.strip = true;
    vary("s1", c);
  } else {
    for (int format = 0; format < 3; format++)
      for (uint32_t factor : {64u, 8u}) {
        c.index_format = (SM50_INDEX_BUFFER_FORMAT)format, c.tess_factor = factor;
        vary("i" + std::to_string(format) + "t" + std::to_string(factor), c);
      }
  }
  auto rootsig = geometry ? SlotsRootSignature({&vs.refl, &primary.refl}) : SlotsRootSignature({&vs.refl, &primary.refl, &ds.refl});
  c = Chain{};
  c.rootsig = &rootsig;
  vary("rs", c);
  return result;
}

// A DXBC shader reached through Translate; hull and geometry files go through TranslatePipeline when named for it.
void Dxbc(id<MTLDevice> device, Shader &s, Result &result) {
  switch (s.entry.stage) {
  case DXIL_SHADER_KIND_COMPUTE:
  case DXIL_SHADER_KIND_VERTEX:
  case DXIL_SHADER_KIND_PIXEL:
    Standalone(device, s, Chain{}, result);
    if (no_metal)
      StandaloneVariants(s, result);
    break;
  case DXIL_SHADER_KIND_DOMAIN:
    result.reason = "no hs for " + Stem(metallib_name);
    break;
  default:
    result.reason = std::string(StageName(s.entry.stage)) + " shaders are named <name>." + StageName(s.entry.stage) + ".dxbc";
  }
}

Result Translate(id<MTLDevice> device, const std::vector<char> &bytes, std::vector<VertexShader> &vertex_shaders,
                 bool last_chance) {
  Result result;
  Shader s;
  if (!s.Init(bytes)) {
    result.reason = s.error;
    return result;
  }
  sm50_shader_t shader = s.handle;
  const DXIL_ENTRY_INFO &entry = s.entry;
  const MTL_SHADER_REFLECTION &refl = s.refl;
  sm50_error_t error = nullptr;
  result.stage = StageName(entry.stage);
  result.refl = s.hash;
  if (init_only) {
    result.ok = true;
    return result;
  }
  if (!entry.is_dxil) {
    Dxbc(device, s, result);
    return result;
  }
  if (entry.stage == DXIL_SHADER_KIND_GEOMETRY) {
    TranslateGeometry(device, shader, entry, vertex_shaders, last_chance, result);
    return result;
  }

  auto rootsig_blob = RootSignature(Resources(entry));
  SM50_SHADER_COMMON_DATA common = Common();
  std::vector<SM50_IA_INPUT_ELEMENT> elements;
  if (entry.stage == DXIL_SHADER_KIND_VERTEX)
    elements = InputLayout(entry);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&common, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE,
                                      elements.empty() ? 0u : 1u, (uint32_t)elements.size(), elements.data()};
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pso{&common, SM50_SHADER_PSO_PIXEL_SHADER, 0xffffffff, false, false, 0, {}};
  for (uint32_t i = 0; i < 8; i++)
    if (refl.PixelShader.ValidRenderTargets & (1 << i))
      pso.pixel_formats[i] = MTLPixelFormatRGBA8Unorm;
  SM50_SHADER_ROOT_SIGNATURE_DATA rootsig{entry.stage == DXIL_SHADER_KIND_VERTEX  ? (void *)&ia
                                          : entry.stage == DXIL_SHADER_KIND_PIXEL ? (void *)&pso
                                                                                  : (void *)&common,
                                          SM50_SHADER_ROOT_SIGNATURE, rootsig_blob.data(), rootsig_blob.size() * 4};

  auto library = [&](sm50_bitcode_t bitcode) { return Library(device, bitcode, result); };
  // A vertex shader that reached a pipeline (or, without Metal, compiled) can feed a geometry shader. `entry` is the
  // shader's, which `s` keeps alive until the return.
  auto keep_vertex = [&] {
    if (entry.stage != DXIL_SHADER_KIND_VERTEX)
      return;
    VertexShader v{bytes, {}};
    for (uint32_t i = 0; i < entry.num_outputs; i++) {
      auto &e = entry.outputs[i];
      if (e.kind == DXIL_SEMANTIC_ARBITRARY || e.kind == DXIL_SEMANTIC_POSITION)
        for (uint32_t r = 0; r < e.rows; r++)
          v.outputs.insert(Semantic(e, r));
    }
    vertex_shaders.push_back(std::move(v));
  };

  sm50_bitcode_t bitcode = nullptr;
  if (SM50Compile(shader, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&rootsig, "main", &bitcode, &error)) {
    result.reason = FirstLine(ErrorText(error));
    return result;
  }
  id<MTLLibrary> lib = library(bitcode);
  if (!device) { // the same translations as with Metal, the pixel shader's pass-through vertex function included
    sm50_bitcode_t vs_bitcode = nullptr;
    if (entry.stage == DXIL_SHADER_KIND_PIXEL && DXILCompilePassThroughVertex(shader, &vs_bitcode, &error)) {
      result.reason = FirstLine(ErrorText(error));
    } else {
      if (vs_bitcode)
        library(vs_bitcode);
      keep_vertex();
      result.ok = true;
    }
    return result;
  }
  id<MTLFunction> function = [lib newFunctionWithName:@"main"];
  if (!function) {
    if (result.reason.empty())
      result.reason = "no function \"main\" in the library";
    return result;
  }

  NSError *err = nil;
  id pipeline = nil;
  if (entry.stage == DXIL_SHADER_KIND_COMPUTE) {
    pipeline = [device newComputePipelineStateWithFunction:function error:&err];
  } else {
    auto desc = [MTLRenderPipelineDescriptor new];
    desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle; // as D3D12 sets it; layered rendering needs it
    if (entry.stage == DXIL_SHADER_KIND_VERTEX) {
      // Metal rejects a non-void vertex function with rasterization off, so an empty fragment function stands in.
      desc.vertexFunction = function;
      desc.fragmentFunction = EmptyFragment(device);
    } else {
      sm50_bitcode_t vs_bitcode = nullptr;
      if (DXILCompilePassThroughVertex(shader, &vs_bitcode, &error)) {
        result.reason = FirstLine(ErrorText(error));
            return result;
      }
      id<MTLLibrary> vs_lib = library(vs_bitcode);
      desc.vertexFunction = [vs_lib newFunctionWithName:@"vs_passthrough"];
      desc.fragmentFunction = function;
      for (uint32_t i = 0; i < 8; i++)
        if (refl.PixelShader.ValidRenderTargets & (1 << i))
          desc.colorAttachments[i].pixelFormat = MTLPixelFormatRGBA8Unorm;
      for (uint32_t i = 0; i < entry.num_outputs; i++) {
        auto kind = entry.outputs[i].kind;
        if (kind == DXIL_SEMANTIC_DEPTH || kind == DXIL_SEMANTIC_DEPTH_LESS_EQUAL ||
            kind == DXIL_SEMANTIC_DEPTH_GREATER_EQUAL || kind == DXIL_SEMANTIC_STENCIL_REF) {
          desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
          desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        }
      }
    }
    pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&err];
  }
  if (pipeline)
    keep_vertex();
  if (!pipeline) {
    result.reason = FirstLine(err ? err.localizedDescription.UTF8String : "no pipeline");
    return result;
  }
  result.ok = true;
  return result;
}

// One result line: `ms` is the time to translate (not to hash and print).
void Print(const std::string &name, Result &r, double ms) { // hashing a digest finishes it
  if (r.ok) {
    if (init_only)
      printf("init %s %s refl=%s\n", name.c_str(), r.stage, r.refl.c_str());
    else if (no_metal)
      printf("ok %s %s %.1f air=%016llx dis=%s refl=%s tags=%s\n", name.c_str(), r.stage, ms, (unsigned long long)r.air,
             r.dis.Hex().c_str(), r.refl.c_str(), r.tags.Hex().c_str());
    else if (count_flags)
      printf("ok %s %s %.1f fence=%s barrier=%u grad=%u/%u nnan=%u ninf=%u cmp=%u reassoc=%u contract=%u arcp=%u vout=%u fdiv=%u/%u\n",
             name.c_str(), r.stage, ms, r.flags.fences.empty() ? "-" : r.flags.fences.c_str(), r.flags.barriers,
             r.flags.scaled_grads, r.flags.grads, r.flags.nnan, r.flags.ninf, r.flags.cmp, r.flags.reassoc, r.flags.contract, r.flags.arcp,
             r.flags.vout, r.flags.fast_fdivs, r.flags.fdivs);
    else
      printf("ok %s %s %.1f air=%016llx\n", name.c_str(), r.stage, ms, (unsigned long long)r.air);
  } else if (no_metal || init_only) {
    printf("fail %s %s refl=%s %s\n", name.c_str(), r.stage, r.refl.c_str(), r.reason.c_str());
  } else {
    printf("fail %s %s\n", name.c_str(), r.reason.c_str());
  }
}

// --pipelines (DXMTRS step 4, G6): the pipelines of a DXMT_DXIL_DUMP capture's pipelines.txt, translated only (no Metal),
// each as D3D12 creates it (src/d3d12/d3d12_pipeline_compute.cpp, d3d12_pipeline_graphics.cpp): the argument chains and
// function names are D3D12's, the root signature is the capture's real rs-<h>.bin (or, for rs=embedded, the shader's
// own bytes), the pixel formats come through D3D12's DXGI -> Metal mapping. The IA layout stays synthetic (InputLayout:
// the vertex shader's inputs as Float4); SampleMask and the blend states past render target 0 are not in the capture
// (all ones, and no further dual-source test). A pipeline is one line, named by its 1-based number; its stage is "cs"
// or "gfx", its refl= the compute shader's own or a hash of the graphics shaders' (vs, ps, gs in order).

// MTLQueryDXGIFormat's PixelFormat for each DXGI format it maps (src/dxmt/dxmt_format.cpp, 98 of them), as
// ORIGINAL_FORMAT leaves it (an extended swizzled format is its base: BGRX8Unorm is BGRA8Unorm). Generated.
struct DxgiFormat {
  uint32_t dxgi;
  MTLPixelFormat metal;
};
const DxgiFormat kDxgiFormats[] = {
    {1, MTLPixelFormatRGBA32Uint}, // R32G32B32A32_TYPELESS
    {2, MTLPixelFormatRGBA32Float}, // R32G32B32A32_FLOAT
    {3, MTLPixelFormatRGBA32Uint}, // R32G32B32A32_UINT
    {4, MTLPixelFormatRGBA32Sint}, // R32G32B32A32_SINT
    {5, MTLPixelFormatR32Uint}, // R32G32B32_TYPELESS
    {6, MTLPixelFormatR32Float}, // R32G32B32_FLOAT
    {7, MTLPixelFormatR32Uint}, // R32G32B32_UINT
    {8, MTLPixelFormatR32Sint}, // R32G32B32_SINT
    {9, MTLPixelFormatRGBA16Float}, // R16G16B16A16_TYPELESS
    {10, MTLPixelFormatRGBA16Float}, // R16G16B16A16_FLOAT
    {11, MTLPixelFormatRGBA16Unorm}, // R16G16B16A16_UNORM
    {12, MTLPixelFormatRGBA16Uint}, // R16G16B16A16_UINT
    {13, MTLPixelFormatRGBA16Snorm}, // R16G16B16A16_SNORM
    {14, MTLPixelFormatRGBA16Sint}, // R16G16B16A16_SINT
    {15, MTLPixelFormatRG32Uint}, // R32G32_TYPELESS
    {16, MTLPixelFormatRG32Float}, // R32G32_FLOAT
    {17, MTLPixelFormatRG32Uint}, // R32G32_UINT
    {18, MTLPixelFormatRG32Sint}, // R32G32_SINT
    {19, MTLPixelFormatDepth32Float_Stencil8}, // R32G8X24_TYPELESS
    {20, MTLPixelFormatDepth32Float_Stencil8}, // D32_FLOAT_S8X24_UINT
    {21, MTLPixelFormatDepth32Float_Stencil8}, // R32_FLOAT_X8X24_TYPELESS (R32X8X32)
    {22, MTLPixelFormatX32_Stencil8}, // X32_TYPELESS_G8X24_UINT (X32G8X32)
    {23, MTLPixelFormatRGB10A2Uint}, // R10G10B10A2_TYPELESS
    {24, MTLPixelFormatRGB10A2Unorm}, // R10G10B10A2_UNORM
    {25, MTLPixelFormatRGB10A2Uint}, // R10G10B10A2_UINT
    {26, MTLPixelFormatRG11B10Float}, // R11G11B10_FLOAT
    {27, MTLPixelFormatRGBA8Unorm}, // R8G8B8A8_TYPELESS
    {28, MTLPixelFormatRGBA8Unorm}, // R8G8B8A8_UNORM
    {29, MTLPixelFormatRGBA8Unorm_sRGB}, // R8G8B8A8_UNORM_SRGB
    {30, MTLPixelFormatRGBA8Uint}, // R8G8B8A8_UINT
    {31, MTLPixelFormatRGBA8Snorm}, // R8G8B8A8_SNORM
    {32, MTLPixelFormatRGBA8Sint}, // R8G8B8A8_SINT
    {33, MTLPixelFormatRG16Uint}, // R16G16_TYPELESS
    {34, MTLPixelFormatRG16Float}, // R16G16_FLOAT
    {35, MTLPixelFormatRG16Unorm}, // R16G16_UNORM
    {36, MTLPixelFormatRG16Uint}, // R16G16_UINT
    {37, MTLPixelFormatRG16Snorm}, // R16G16_SNORM
    {38, MTLPixelFormatRG16Sint}, // R16G16_SINT
    {39, MTLPixelFormatR32Uint}, // R32_TYPELESS
    {40, MTLPixelFormatDepth32Float}, // D32_FLOAT
    {41, MTLPixelFormatR32Float}, // R32_FLOAT
    {42, MTLPixelFormatR32Uint}, // R32_UINT
    {43, MTLPixelFormatR32Sint}, // R32_SINT
    {44, MTLPixelFormatDepth32Float_Stencil8}, // R24G8_TYPELESS
    {45, MTLPixelFormatDepth32Float_Stencil8}, // D24_UNORM_S8_UINT
    {46, MTLPixelFormatDepth32Float_Stencil8}, // R24_UNORM_X8_TYPELESS (R32X8X32)
    {47, MTLPixelFormatX32_Stencil8}, // X24_TYPELESS_G8_UINT (X32G8X32)
    {48, MTLPixelFormatRG8Uint}, // R8G8_TYPELESS
    {49, MTLPixelFormatRG8Unorm}, // R8G8_UNORM
    {50, MTLPixelFormatRG8Uint}, // R8G8_UINT
    {51, MTLPixelFormatRG8Snorm}, // R8G8_SNORM
    {52, MTLPixelFormatRG8Sint}, // R8G8_SINT
    {53, MTLPixelFormatR16Uint}, // R16_TYPELESS
    {54, MTLPixelFormatR16Float}, // R16_FLOAT
    {55, MTLPixelFormatDepth16Unorm}, // D16_UNORM
    {56, MTLPixelFormatR16Unorm}, // R16_UNORM
    {57, MTLPixelFormatR16Uint}, // R16_UINT
    {58, MTLPixelFormatR16Snorm}, // R16_SNORM
    {59, MTLPixelFormatR16Sint}, // R16_SINT
    {60, MTLPixelFormatR8Uint}, // R8_TYPELESS
    {61, MTLPixelFormatR8Unorm}, // R8_UNORM
    {62, MTLPixelFormatR8Uint}, // R8_UINT
    {63, MTLPixelFormatR8Snorm}, // R8_SNORM
    {64, MTLPixelFormatR8Sint}, // R8_SINT
    {65, MTLPixelFormatA8Unorm}, // A8_UNORM
    {67, MTLPixelFormatRGB9E5Float}, // R9G9B9E5_SHAREDEXP
    {68, MTLPixelFormatBGRG422}, // R8G8_B8G8_UNORM
    {69, MTLPixelFormatGBGR422}, // G8R8_G8B8_UNORM
    {70, MTLPixelFormatBC1_RGBA}, // BC1_TYPELESS
    {71, MTLPixelFormatBC1_RGBA}, // BC1_UNORM
    {72, MTLPixelFormatBC1_RGBA_sRGB}, // BC1_UNORM_SRGB
    {73, MTLPixelFormatBC2_RGBA}, // BC2_TYPELESS
    {74, MTLPixelFormatBC2_RGBA}, // BC2_UNORM
    {75, MTLPixelFormatBC2_RGBA_sRGB}, // BC2_UNORM_SRGB
    {76, MTLPixelFormatBC3_RGBA}, // BC3_TYPELESS
    {77, MTLPixelFormatBC3_RGBA}, // BC3_UNORM
    {78, MTLPixelFormatBC3_RGBA_sRGB}, // BC3_UNORM_SRGB
    {79, MTLPixelFormatBC4_RUnorm}, // BC4_TYPELESS
    {80, MTLPixelFormatBC4_RUnorm}, // BC4_UNORM
    {81, MTLPixelFormatBC4_RSnorm}, // BC4_SNORM
    {82, MTLPixelFormatBC5_RGUnorm}, // BC5_TYPELESS
    {83, MTLPixelFormatBC5_RGUnorm}, // BC5_UNORM
    {84, MTLPixelFormatBC5_RGSnorm}, // BC5_SNORM
    {85, MTLPixelFormatB5G6R5Unorm}, // B5G6R5_UNORM
    {86, MTLPixelFormatBGR5A1Unorm}, // B5G5R5A1_UNORM
    {87, MTLPixelFormatBGRA8Unorm}, // B8G8R8A8_UNORM
    {88, MTLPixelFormatBGRA8Unorm}, // B8G8R8X8_UNORM (BGRX8Unorm)
    {90, MTLPixelFormatBGRA8Unorm}, // B8G8R8A8_TYPELESS
    {91, MTLPixelFormatBGRA8Unorm_sRGB}, // B8G8R8A8_UNORM_SRGB
    {92, MTLPixelFormatBGRA8Unorm}, // B8G8R8X8_TYPELESS (BGRX8Unorm)
    {93, MTLPixelFormatBGRA8Unorm_sRGB}, // B8G8R8X8_UNORM_SRGB (BGRX8Unorm_sRGB)
    {94, MTLPixelFormatBC6H_RGBUfloat}, // BC6H_TYPELESS
    {95, MTLPixelFormatBC6H_RGBUfloat}, // BC6H_UF16
    {96, MTLPixelFormatBC6H_RGBFloat}, // BC6H_SF16
    {97, MTLPixelFormatBC7_RGBAUnorm}, // BC7_TYPELESS
    {98, MTLPixelFormatBC7_RGBAUnorm}, // BC7_UNORM
    {99, MTLPixelFormatBC7_RGBAUnorm_sRGB}, // BC7_UNORM_SRGB
    {115, MTLPixelFormatABGR4Unorm}, // B4G4R4A4_UNORM (BGRA4Unorm)
};

MTLPixelFormat MetalFormat(uint32_t dxgi) {
  for (auto &f : kDxgiFormats)
    if (f.dxgi == dxgi)
      return f.metal;
  return MTLPixelFormatInvalid;
}

struct PipelineLine {
  bool compute = false;
  std::string cs, vs, ps, gs, rs; // 16 hex digits (the capture's FNV-1a 64); rs may be "embedded"; "" is absent
  std::vector<uint32_t> rtv;      // DXGI formats
  uint32_t dsv = 0;
  bool dual = false; // blend0 names a dual-source factor (D3D12_BLEND_SRC1_COLOR .. INV_SRC1_ALPHA)
};

bool IsHash(const std::string &s) {
  return s.size() == 16 && std::all_of(s.begin(), s.end(), [](char c) { return isdigit((unsigned char)c) || (c >= 'a' && c <= 'f'); });
}

bool Number(const std::string &s, uint32_t &value) {
  if (s.empty() || s.size() > 10 || !std::all_of(s.begin(), s.end(), [](char c) { return isdigit((unsigned char)c); }))
    return false;
  unsigned long long v = strtoull(s.c_str(), nullptr, 10);
  value = (uint32_t)v;
  return v <= 0xffffffffull;
}

std::vector<std::string> Split(const std::string &s, char separator) {
  std::vector<std::string> parts;
  for (size_t at = 0;;) {
    auto end = s.find(separator, at);
    parts.push_back(s.substr(at, end == std::string::npos ? end : end - at));
    if (end == std::string::npos)
      return parts;
    at = end + 1;
  }
}

// A pipelines.txt line as the writers emit it: "cs cs=<h> rs=<h>" (d3d12_pipeline_compute.cpp:46) or "gfx [vs=<h>]
// [ps=<h>] [gs=<h>] rs=<h> rtv=<n>,.. dsv=<n> topology=<n> samples=<n> blend0=off|<six n> mask0=<n>"
// (d3d12_pipeline_graphics.cpp:617-633; d3d12_pipeline.hpp:88-104's CapturedShader and CapturedRootSignature). Returns ""
// or what is wrong with the line.
std::string ParseLine(const std::string &line, PipelineLine &p) {
  auto t = Split(line, ' ');
  size_t i = 1;
  std::string v, err;
  auto take = [&](const char *key) {
    std::string prefix = std::string(key) + "=";
    if (i < t.size() && !t[i].compare(0, prefix.size(), prefix)) {
      v = t[i++].substr(prefix.size());
      return true;
    }
    return false;
  };
  auto need = [&](const char *key) { return take(key) || ((err = std::string("expected ") + key + "="), false); };
  auto hash = [&](std::string &out, bool embedded = false) {
    if (!IsHash(v) && !(embedded && v == "embedded"))
      return (err = "not a hash: " + v), false;
    return (out = v), true;
  };
  auto number = [&](uint32_t &out) { return Number(v, out) || ((err = "not a number: " + v), false); };
  uint32_t unused = 0;
  if (t[0] == "cs") {
    p.compute = true;
    if (!need("cs") || !hash(p.cs) || !need("rs") || !hash(p.rs, true))
      return err;
  } else if (t[0] == "gfx") {
    if ((take("vs") && !hash(p.vs)) || (take("ps") && !hash(p.ps)) || (take("gs") && !hash(p.gs)))
      return err;
    if (!need("rs") || !hash(p.rs, true) || !need("rtv"))
      return err;
    for (auto &item : v.empty() ? std::vector<std::string>{} : Split(v, ',')) {
      if (p.rtv.size() == 8)
        return "more than 8 render targets";
      v = item;
      if (!number(unused))
        return err;
      p.rtv.push_back(unused);
    }
    if (!need("dsv") || !number(p.dsv) || !need("topology") || !number(unused) || !need("samples") || !number(unused) ||
        !need("blend0"))
      return err;
    if (v != "off") {
      auto factors = Split(v, ',');
      if (factors.size() != 6)
        return "blend0 is not off or six numbers";
      for (size_t k = 0; k < 6; k++) {
        v = factors[k];
        if (!number(unused))
          return err;
        p.dual |= unused >= 16 && unused <= 19 && k != 2 && k != 5; // SrcBlend, DestBlend, SrcBlendAlpha, DestBlendAlpha
      }
    }
    if (!need("mask0") || !number(unused))
      return err;
  } else {
    return "neither a cs nor a gfx line";
  }
  return i == t.size() ? "" : "unexpected " + t[i];
}

// One shader file of a pipeline, read and initialized.
struct Loaded {
  std::vector<char> bytes;
  Shader shader;
};

bool ReadFile(const std::filesystem::path &path, std::vector<char> &bytes) {
  std::ifstream in(path, std::ios::binary);
  if (!in)
    return false;
  bytes.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
  return true;
}

// Whether a DXBC container's part list names `fourcc`. The C++ translator dereferences a null where a root signature
// blob has no RTS0 part (the shader's own, with rs=embedded, or a damaged rs-<h>.bin), which would end the whole run.
bool HasPart(const std::vector<char> &b, const char *fourcc) {
  uint32_t count = 0;
  if (b.size() < 32 || memcmp(b.data(), "DXBC", 4))
    return false;
  memcpy(&count, b.data() + 28, 4);
  for (uint32_t i = 0; i < count && 32 + 4ull * (i + 1) <= b.size(); i++) {
    uint32_t offset = 0;
    memcpy(&offset, b.data() + 32 + 4 * i, 4);
    if (offset + 4ull <= b.size() && !memcmp(b.data() + offset, fourcc, 4))
      return true;
  }
  return false;
}

// The pipeline of one pipelines.txt line, translated as D3D12 compiles it at creation. The Initialize calls come
// first (D3D12 interleaves them with the compiles; a line that fails twice shows the first Initialize's reason).
Result Pipeline(const std::string &line) {
  Result r;
  PipelineLine p;
  if (auto err = ParseLine(line, p); !err.empty()) {
    r.reason = "pipelines.txt: " + err;
    return r;
  }
  Loaded cs, vs, ps, gs;
  struct {
    const char *role;
    const std::string &hash;
    Loaded &loaded;
  } parts[] = {{"cs", p.cs, cs}, {"vs", p.vs, vs}, {"ps", p.ps, ps}, {"gs", p.gs, gs}};
  Sha256 refl;
  for (auto &part : parts) {
    if (part.hash.empty())
      continue;
    auto file = std::string(part.role) + "-" + part.hash + ".dxil";
    if (!ReadFile(std::filesystem::path(folder) / file, part.loaded.bytes)) {
      r.reason = "can't read " + file;
      return r;
    }
    if (!part.loaded.shader.Init(part.loaded.bytes)) {
      r.reason = std::string(part.role) + ": " + part.loaded.shader.error;
      return r;
    }
    refl.Add(part.role, 2), refl.Add(part.loaded.shader.hash.data(), part.loaded.shader.hash.size());
  }
  r.stage = p.compute ? "cs" : "gfx";
  r.refl = p.compute ? cs.shader.hash : refl.Hex();
  if (!p.compute && p.vs.empty()) {
    r.reason = "no vertex shader";
    return r;
  }
  std::vector<char> blob; // the real root signature
  if (p.rs != "embedded" && !ReadFile(std::filesystem::path(folder) / ("rs-" + p.rs + ".bin"), blob)) {
    r.reason = "can't read rs-" + p.rs + ".bin";
    return r;
  }
  for (auto &part : parts) // every shader but the geometry shader is given the root signature
    if (p.rs == "embedded" && !part.hash.empty() && strcmp(part.role, "gs") && !HasPart(part.loaded.bytes, "RTS0")) {
      r.reason = std::string("rs=embedded: ") + part.role + " has no RTS0 part";
      return r;
    }
  if (p.rs != "embedded" && !HasPart(blob, "RTS0")) {
    r.reason = "rs-" + p.rs + ".bin has no RTS0 part";
    return r;
  }
  auto rootsig = [&](const Loaded &own, void *next) {
    auto &bytes = p.rs == "embedded" ? own.bytes : blob;
    return SM50_SHADER_ROOT_SIGNATURE_DATA{next, SM50_SHADER_ROOT_SIGNATURE, bytes.data(), bytes.size()};
  };
  // A translation's end: its metallib recorded, or the compiler's refusal as the reason.
  sm50_bitcode_t bitcode = nullptr;
  sm50_error_t error = nullptr;
  auto done = [&](int failed) {
    if (failed)
      r.reason = FirstLine(ErrorText(error));
    else
      Library(nil, bitcode, r);
    bitcode = nullptr, error = nullptr;
    return !failed;
  };
  using Args = SM50_SHADER_COMPILATION_ARGUMENT_DATA;

  if (p.compute) { // the compute chain is common -> root signature
    SM50_SHADER_COMMON_DATA common = Common();
    auto rootsig_node = rootsig(cs, nullptr);
    common.next = &rootsig_node;
    r.ok = done(SM50Compile(cs.shader.handle, (Args *)&common, "cs_main", &bitcode, &error));
    return r;
  }

  uint32_t pixel_formats[8] = {};
  // InitializePSO: the render target and depth formats, which D3D12 maps whether or not there is a pixel shader.
  auto formats = [&] {
    for (size_t i = 0; i < p.rtv.size(); i++) {
      if (!p.rtv[i])
        continue;
      if (i >= 2 && p.dual)
        break;
      pixel_formats[i] = (uint32_t)MetalFormat(p.rtv[i]);
      if (!pixel_formats[i])
        return (r.reason = "no Metal pixel format for DXGI format " + std::to_string(p.rtv[i])), false;
    }
    if (p.dsv && !MetalFormat(p.dsv))
      return (r.reason = "no Metal pixel format for DXGI format " + std::to_string(p.dsv)), false;
    return true;
  };
  // CompilePixelShader: root signature -> pixel PSO data -> common, named "ps_main" and the shader's first 8 hash
  // digits (D3D12 takes them from the bytecode's SHA-1).
  auto pixel = [&] {
    if (p.ps.empty())
      return true;
    SM50_SHADER_COMMON_DATA common = Common();
    SM50_SHADER_PSO_PIXEL_SHADER_DATA pso{&common, SM50_SHADER_PSO_PIXEL_SHADER, 0xffffffff, p.dual, false, 0, {}};
    std::copy(std::begin(pixel_formats), std::end(pixel_formats), pso.pixel_formats);
    auto rootsig_node = rootsig(ps, &pso);
    auto name = "ps_main" + p.ps.substr(0, 8);
    return done(SM50Compile(ps.shader.handle, (Args *)&rootsig_node, name.c_str(), &bitcode, &error));
  };
  auto elements = InputLayout(vs.shader.entry);
  uint32_t slot_mask = elements.empty() ? 0u : 1u; // every element is in slot 0
  SM50_SHADER_COMMON_DATA common = Common();

  if (!p.gs.empty()) { // InitializeGeometry: the pixel shader, then the triangle-list non-indexed variant
    SM50_SHADER_PSO_GEOMETRY_SHADER_DATA gs_data{&common, SM50_SHADER_PSO_GEOMETRY_SHADER, false};
    SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&gs_data, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE, slot_mask,
                                        (uint32_t)elements.size(), elements.data()};
    auto object_args = rootsig(vs, &ia), mesh_args = rootsig(vs, &gs_data);
    r.ok = formats() && pixel() &&
           done(SM50CompileGeometryPipelineVertex(vs.shader.handle, gs.shader.handle, (Args *)&object_args, "vsgs_main",
                                                  &bitcode, &error)) &&
           done(SM50CompileGeometryPipelineGeometry(vs.shader.handle, gs.shader.handle, (Args *)&mesh_args, "gs_main",
                                                    &bitcode, &error));
    return r;
  }
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&common, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE, slot_mask,
                                      (uint32_t)elements.size(), elements.data()};
  auto vs_args = rootsig(vs, &ia);
  r.ok = done(SM50Compile(vs.shader.handle, (Args *)&vs_args, "vs_main", &bitcode, &error)) && formats() && pixel();
  return r;
}

int Run() {
  std::error_code ec;
  std::vector<std::filesystem::path> files;
  for (auto &f : std::filesystem::directory_iterator(folder, ec))
    if (f.path().extension() == ".dxil" || f.path().extension() == ".dxbc")
      files.push_back(f.path());
  if (ec) {
    fprintf(stderr, "dxil-translate: can't read %s: %s\n", folder, ec.message().c_str());
    return 2;
  }
  std::sort(files.begin(), files.end());
  id<MTLDevice> device = no_metal || init_only ? nil : MTLCreateSystemDefaultDevice();
  uint32_t ok = 0, units = 0; // a DXBC pipeline group is one unit; --initialize-only lists every file
  double total_ms = 0, slowest_ms = 0;
  std::string slowest = "-";
  std::vector<VertexShader> vertex_shaders;
  std::vector<std::filesystem::path> deferred; // geometry shaders whose vertex shader may come later
  std::set<std::string> in_group;              // domain shaders printed on their hull shader's line
  for (auto &path : files)
    if (!init_only && EndsWith(path.filename().string(), ".hs.dxbc"))
      in_group.insert(Stem(path.filename().string()) + ".ds.dxbc");
  auto translate = [&](const std::filesystem::path &path, bool last_chance) {
    auto name = path.filename().string();
    if (in_group.count(name))
      return;
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    metallib_name = name, metallib_count = 0, tool_ms = 0;
    auto start = std::chrono::steady_clock::now();
    Result r;
    @autoreleasepool {
      if (!init_only && (EndsWith(name, ".hs.dxbc") || EndsWith(name, ".gs.dxbc")))
        r = TranslatePipeline(device, name, bytes);
      else
        r = Translate(device, bytes, vertex_shaders, last_chance);
    }
    if (r.deferred) {
      deferred.push_back(path);
      return;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() - tool_ms;
    units++;
    if (r.ok) {
      ok++;
      total_ms += ms;
      if (ms > slowest_ms)
        slowest_ms = ms, slowest = name;
    }
    Print(name, r, ms);
    for (auto &variant : r.variants)
      Print(name + "#" + variant.suffix, variant, variant.ms);
    fflush(stdout);
  };
  for (auto &path : files)
    translate(path, false);
  for (auto &path : std::vector(deferred))
    translate(path, true);
  printf("summary %u/%u ok, mean %.1f ms, slowest %.1f ms %s\n", ok, units, ok ? total_ms / ok : 0.0, slowest_ms,
         slowest.c_str());
  return ok == units && units ? 0 : 1;
}

// --pipelines: one result line per line of the pipelines file (--line <n> runs only line n).
int RunPipelines() {
  std::ifstream in(pipelines_file);
  if (!in) {
    fprintf(stderr, "dxil-translate: can't read %s\n", pipelines_file);
    return 2;
  }
  uint32_t ok = 0, units = 0, number = 0;
  double total_ms = 0, slowest_ms = 0;
  std::string slowest = "-", line;
  while (std::getline(in, line)) {
    number++;
    if (only_line && number != only_line)
      continue;
    auto name = std::to_string(number);
    metallib_name = name, metallib_count = 0, tool_ms = 0;
    auto start = std::chrono::steady_clock::now();
    Result r = Pipeline(line);
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count() - tool_ms;
    units++;
    if (r.ok) {
      ok++;
      total_ms += ms;
      if (ms > slowest_ms)
        slowest_ms = ms, slowest = name;
    }
    Print(name, r, ms);
    fflush(stdout);
  }
  printf("summary %u/%u ok, mean %.1f ms, slowest %.1f ms %s\n", ok, units, ok ? total_ms / ok : 0.0, slowest_ms,
         slowest.c_str());
  return ok == units && units ? 0 : 1;
}

} // namespace

int main(int argc, char **argv) {
  bool usage = false;
  for (int i = 1; i < argc; i++) {
    std::string arg = argv[i];
    char *end = nullptr;
    if (arg == "--flags")
      count_flags = true;
    else if (arg == "--no-metal")
      no_metal = true;
    else if (arg == "--initialize-only")
      init_only = true;
    else if ((arg == "--out" || arg == "--use" || arg == "--dis") && i + 1 < argc)
      (arg == "--out" ? out_dir : arg == "--use" ? use_dir : dis_dir) = argv[++i];
    else if (arg == "--metal" && i + 1 < argc) {
      std::string version = argv[++i];
      usage |= version != "310" && version != "320";
      metal_version = version == "320" ? SM50_SHADER_METAL_320 : SM50_SHADER_METAL_310;
    } else if (arg == "--pipelines" && i + 1 < argc && !pipelines_file) {
      pipelines_file = argv[++i];
    } else if (arg == "--line" && i + 1 < argc) {
      only_line = strtoul(argv[++i], &end, 0);
      usage |= !isdigit((unsigned char)*argv[i]) || *end || !only_line;
    } else if (arg == "--shader-flags" && i + 1 < argc) {
      shader_flags = strtoul(argv[++i], &end, 0);
      usage |= !isdigit((unsigned char)*argv[i]) || *end;
    } else if (arg.rfind("-", 0) == 0 || folder)
      usage = true;
    else
      folder = argv[i];
  }
  // --pipelines <file> replaces <folder>: the shaders and root signatures are read from the file's folder.
  std::string pipelines_dir = pipelines_file ? std::filesystem::path(pipelines_file).parent_path().string() : "";
  if (pipelines_file) {
    usage |= folder || !no_metal || init_only;
    folder = pipelines_dir.empty() ? "." : pipelines_dir.c_str();
  }
  usage |= only_line && !pipelines_file;
  if (usage || !folder) {
    fprintf(stderr, "usage: dxil-translate <folder> [--flags] [--out <dir>] [--use <dir>] [--dis <dir>] [--no-metal | "
                    "--initialize-only] [--metal 310|320] [--shader-flags <n>]\n"
                    "       dxil-translate --pipelines <pipelines.txt> --no-metal [--line <n>] [--dis <dir>] [--out <dir>] "
                    "[--metal 310|320] [--shader-flags <n>]\n");
    return 2;
  }
  std::error_code ec;
  for (auto *dir : {&out_dir, &dis_dir})
    if (!dir->empty() && !std::filesystem::create_directories(*dir, ec) && ec) {
      fprintf(stderr, "dxil-translate: can't create %s: %s\n", dir->c_str(), ec.message().c_str());
      return 2;
    }
  // Every translation on one 256 KiB thread (DXMTRS step 4, P4): a deeper translator overflows here first.
  pthread_attr_t attr;
  pthread_t thread;
  int rc = 2;
  pthread_attr_init(&attr);
  pthread_attr_setstacksize(&attr, 256 << 10);
  auto run = [](void *rc) -> void * {
    *(int *)rc = pipelines_file ? RunPipelines() : Run();
    return nullptr;
  };
  if (pthread_create(&thread, &attr, run, &rc)) {
    fprintf(stderr, "dxil-translate: can't start the translation thread\n");
    return 2;
  }
  pthread_join(thread, nullptr);
  return rc;
}
