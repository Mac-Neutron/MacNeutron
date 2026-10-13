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
// offline).
#import <Metal/Metal.h>
#define BOOL WIN_BOOL // airconv's Windows headers define BOOL as int; Objective-C's is signed char
#include "airconv_public.h"
#include "dxbc_converter.hpp"
#include "dxil/dxil_public.h"
#include "llvm/Bitcode/BitcodeReader.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/LLVMContext.h"
#include "llvm/IR/Module.h"
#include "llvm/IR/Operator.h"
#include "llvm/Support/MemoryBuffer.h"
#undef BOOL
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <set>
#include <string>
#include <vector>

using namespace dxmt::dxil;

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

// A DXBC container with an RTS0 (root signature 1.0) part: one descriptor table per resource range, visible to all
// stages, each range at offset 0 of its own table (so an unbounded range can't push another one out of place). Heap
// ranges get none: the flags say both heaps are directly indexed (0xC00), so a shader that indexes one reads it through
// the heap bases after the parameters, as D3D12 binds it (DXIL SM 6.8 plan, Task 9); other shaders gain nothing.
std::vector<uint32_t> RootSignature(const std::vector<Resource> &all) {
  const uint32_t header = 6, param = 3, table = 2, range = 5;
  std::vector<Resource> resources;
  std::copy_if(all.begin(), all.end(), std::back_inserter(resources), [](const Resource &r) { return !r.heap; });
  uint32_t n = resources.size();
  std::vector<uint32_t> rts0{1, n, header * 4, 0, 0, 0xC00};
  for (uint32_t i = 0; i < n; i++) {
    uint32_t payload = (header + param * n + (table + range) * i) * 4;
    rts0.insert(rts0.end(), {0 /* table */, 0 /* all stages */, payload});
  }
  for (uint32_t i = 0; i < n; i++) {
    auto &r = resources[i];
    uint32_t ranges = (header + param * n + (table + range) * i + table) * 4;
    uint32_t type = r.cls == ResourceClass::SRV ? 0 : r.cls == ResourceClass::UAV ? 1 : r.cls == ResourceClass::CBuffer ? 2 : 3;
    rts0.insert(rts0.end(), {1, ranges, type, r.size, r.lower_bound, r.space, 0});
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
  const char *stage = "";
  std::string reason;
  Flags flags; // --flags: summed over the pipeline's translated functions
  uint64_t air = 0xcbf29ce484222325; // FNV-1a 64 of every translated metallib's bytes, in translation order
};

bool count_flags = false;

// Counts the fast-math flags of a translated metallib's AIR (its bitcode starts at the wrapper magic).
void CountFlags(const void *data, size_t size, Flags &flags) {
  std::string_view bytes((const char *)data, size);
  auto at = bytes.find(std::string_view("\xde\xc0\x17\x0b", 4));
  if (at == std::string_view::npos)
    return;
  auto buffer = llvm::MemoryBuffer::getMemBuffer(llvm::StringRef(bytes.data() + at, size - at), "", false);
  llvm::LLVMContext context;
  context.setOpaquePointers(false);
  auto module = llvm::parseBitcodeFile(buffer->getMemBufferRef(), context);
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

// A vertex shader seen in the folder, for pairing with geometry shaders: its bytecode and output semantics.
struct VertexShader {
  std::vector<char> bytes;
  std::set<std::string> outputs;
};

std::string Semantic(const SignatureElement &e, uint32_t row) {
  return e.kind == SemanticKind::Position ? "SV_POSITION" : UserName(e, row);
}

// One R32G32B32A32_FLOAT input layout element per vertex shader input register, slot 0.
std::vector<SM50_IA_INPUT_ELEMENT> InputLayout(const EntryInfo &vs) {
  std::vector<SM50_IA_INPUT_ELEMENT> elements;
  for (auto &e : vs.inputs)
    if (e.kind == SemanticKind::Arbitrary)
      for (uint32_t r = 0; r < e.rows; r++)
        elements.push_back({uint32_t(e.start_row) + r, 0, 16 * (uint32_t(e.start_row) + r), MTLAttributeFormatFloat4, 0, 0});
  return elements;
}

id<MTLLibrary> Library(id<MTLDevice> device, sm50_bitcode_t bitcode, Result &result) {
  SM50_COMPILED_BITCODE data;
  SM50GetCompiledBitcode(bitcode, &data);
  if (count_flags)
    CountFlags(data.Data, data.Size, result.flags);
  for (size_t i = 0; i < data.Size; i++)
    result.air = (result.air ^ ((const uint8_t *)data.Data)[i]) * 0x100000001b3;
  auto dispatch = dispatch_data_create(data.Data, data.Size, nullptr, DISPATCH_DATA_DESTRUCTOR_DEFAULT);
  SM50DestroyBitcode(bitcode);
  NSError *err = nil;
  id<MTLLibrary> lib = [device newLibraryWithData:dispatch error:&err];
  if (!lib)
    result.reason = FirstLine(err.localizedDescription.UTF8String);
  return lib;
}

// A geometry shader's mesh pipeline: `vs` (a folder vertex shader covering its inputs) as the object function.
void TranslateGeometry(id<MTLDevice> device, sm50_shader_t gs, const std::vector<VertexShader> &vertex_shaders,
                       bool last_chance, Result &result) {
  auto &entry = ((dxmt::dxbc::SM50ShaderInternal *)gs)->dxil->entry;
  std::set<std::string> needs;
  for (auto &e : entry.inputs)
    if (e.kind == SemanticKind::Arbitrary || e.kind == SemanticKind::Position)
      for (uint32_t r = 0; r < e.rows; r++)
        needs.insert(Semantic(e, r));
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
  auto &vs_entry = ((dxmt::dxbc::SM50ShaderInternal *)vs)->dxil->entry;
  auto resources = vs_entry.resources;
  resources.insert(resources.end(), entry.resources.begin(), entry.resources.end());
  auto rootsig_blob = RootSignature(resources);
  SM50_SHADER_COMMON_DATA common{nullptr, SM50_SHADER_COMMON, SM50_SHADER_METAL_310, {}};
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
  auto desc = [MTLMeshRenderPipelineDescriptor new];
  desc.objectFunction = [Library(device, object_bitcode, result) newFunctionWithName:@"vs_object"];
  desc.meshFunction = [Library(device, mesh_bitcode, result) newFunctionWithName:@"gs_mesh"];
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

Result Translate(id<MTLDevice> device, const std::vector<char> &bytes, std::vector<VertexShader> &vertex_shaders,
                 bool last_chance) {
  Result result;
  sm50_shader_t shader = nullptr;
  sm50_error_t error = nullptr;
  MTL_SHADER_REFLECTION refl{};
  if (SM50Initialize(bytes.data(), bytes.size(), &shader, &refl, &error)) {
    result.reason = FirstLine(ErrorText(error));
    return result;
  }
  auto internal = (dxmt::dxbc::SM50ShaderInternal *)shader;
  if (!internal->dxil && internal->shader_type == microsoft::D3D11_SB_COMPUTE_SHADER) {
    // A DXBC compute shader (Task FR): D3D11's call (d3d11_shader.cpp), no root signature, at Metal 3.1 as the rest.
    result.stage = "cs";
    SM50_SHADER_COMMON_DATA common{nullptr, SM50_SHADER_COMMON, SM50_SHADER_METAL_310, {}};
    sm50_bitcode_t bitcode = nullptr;
    if (SM50Compile(shader, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&common, "main", &bitcode, &error)) {
      result.reason = FirstLine(ErrorText(error));
    } else if (id<MTLFunction> function = [Library(device, bitcode, result) newFunctionWithName:@"main"]) {
      NSError *err = nil;
      result.ok = [device newComputePipelineStateWithFunction:function error:&err] != nil;
      if (!result.ok)
        result.reason = FirstLine(err ? err.localizedDescription.UTF8String : "no pipeline");
    } else if (result.reason.empty()) {
      result.reason = "no function \"main\" in the library";
    }
    SM50Destroy(shader);
    return result;
  }
  if (!internal->dxil) {
    SM50Destroy(shader);
    result.reason = "not a DXIL shader";
    return result;
  }
  auto &entry = internal->dxil->entry;
  result.stage = entry.kind == ShaderKind::Vertex     ? "vs"
                 : entry.kind == ShaderKind::Pixel    ? "ps"
                 : entry.kind == ShaderKind::Geometry ? "gs"
                                                      : "cs";
  if (entry.kind == ShaderKind::Geometry) {
    TranslateGeometry(device, shader, vertex_shaders, last_chance, result);
    SM50Destroy(shader);
    return result;
  }

  auto rootsig_blob = RootSignature(entry.resources);
  SM50_SHADER_COMMON_DATA common{nullptr, SM50_SHADER_COMMON, SM50_SHADER_METAL_310, {}};
  std::vector<SM50_IA_INPUT_ELEMENT> elements;
  if (entry.kind == ShaderKind::Vertex)
    elements = InputLayout(entry);
  SM50_SHADER_IA_INPUT_LAYOUT_DATA ia{&common, SM50_SHADER_IA_INPUT_LAYOUT, SM50_INDEX_BUFFER_FORMAT_NONE,
                                      elements.empty() ? 0u : 1u, (uint32_t)elements.size(), elements.data()};
  SM50_SHADER_PSO_PIXEL_SHADER_DATA pso{&common, SM50_SHADER_PSO_PIXEL_SHADER, 0xffffffff, false, false, 0, {}};
  for (uint32_t i = 0; i < 8; i++)
    if (refl.PixelShader.ValidRenderTargets & (1 << i))
      pso.pixel_formats[i] = MTLPixelFormatRGBA8Unorm;
  SM50_SHADER_ROOT_SIGNATURE_DATA rootsig{entry.kind == ShaderKind::Vertex  ? (void *)&ia
                                          : entry.kind == ShaderKind::Pixel ? (void *)&pso
                                                                            : (void *)&common,
                                          SM50_SHADER_ROOT_SIGNATURE, rootsig_blob.data(), rootsig_blob.size() * 4};

  auto library = [&](sm50_bitcode_t bitcode) { return Library(device, bitcode, result); };

  sm50_bitcode_t bitcode = nullptr;
  if (SM50Compile(shader, (SM50_SHADER_COMPILATION_ARGUMENT_DATA *)&rootsig, "main", &bitcode, &error)) {
    result.reason = FirstLine(ErrorText(error));
    SM50Destroy(shader);
    return result;
  }
  id<MTLLibrary> lib = library(bitcode);
  id<MTLFunction> function = [lib newFunctionWithName:@"main"];
  if (!function) {
    if (result.reason.empty())
      result.reason = "no function \"main\" in the library";
    SM50Destroy(shader);
    return result;
  }

  NSError *err = nil;
  id pipeline = nil;
  if (entry.kind == ShaderKind::Compute) {
    pipeline = [device newComputePipelineStateWithFunction:function error:&err];
  } else {
    auto desc = [MTLRenderPipelineDescriptor new];
    desc.inputPrimitiveTopology = MTLPrimitiveTopologyClassTriangle; // as D3D12 sets it; layered rendering needs it
    if (entry.kind == ShaderKind::Vertex) {
      // Metal rejects a non-void vertex function with rasterization off, so an empty fragment function stands in.
      desc.vertexFunction = function;
      static id<MTLFunction> empty = [[device newLibraryWithSource:@"fragment void ps_empty() {}" options:nil error:nil]
          newFunctionWithName:@"ps_empty"];
      desc.fragmentFunction = empty;
    } else {
      sm50_bitcode_t vs_bitcode = nullptr;
      if (DXILCompilePassThroughVertex(shader, &vs_bitcode, &error)) {
        result.reason = FirstLine(ErrorText(error));
        SM50Destroy(shader);
        return result;
      }
      id<MTLLibrary> vs_lib = library(vs_bitcode);
      desc.vertexFunction = [vs_lib newFunctionWithName:@"vs_passthrough"];
      desc.fragmentFunction = function;
      for (uint32_t i = 0; i < 8; i++)
        if (refl.PixelShader.ValidRenderTargets & (1 << i))
          desc.colorAttachments[i].pixelFormat = MTLPixelFormatRGBA8Unorm;
      for (auto &e : entry.outputs)
        if (e.kind == SemanticKind::Depth || e.kind == SemanticKind::DepthLessEqual ||
            e.kind == SemanticKind::DepthGreaterEqual || e.kind == SemanticKind::StencilRef) {
          desc.depthAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
          desc.stencilAttachmentPixelFormat = MTLPixelFormatDepth32Float_Stencil8;
        }
    }
    pipeline = [device newRenderPipelineStateWithDescriptor:desc error:&err];
  }
  if (pipeline && entry.kind == ShaderKind::Vertex) { // before SM50Destroy: `entry` is the shader's
    VertexShader v{bytes, {}};
    for (auto &e : entry.outputs)
      if (e.kind == SemanticKind::Arbitrary || e.kind == SemanticKind::Position)
        for (uint32_t r = 0; r < e.rows; r++)
          v.outputs.insert(Semantic(e, r));
    vertex_shaders.push_back(std::move(v));
  }
  SM50Destroy(shader);
  if (!pipeline) {
    result.reason = FirstLine(err ? err.localizedDescription.UTF8String : "no pipeline");
    return result;
  }
  result.ok = true;
  return result;
}

} // namespace

int main(int argc, char **argv) {
  count_flags = argc == 3 && std::string(argv[2]) == "--flags";
  if (argc != 2 && !count_flags) {
    fprintf(stderr, "usage: dxil-translate <folder> [--flags]\n");
    return 2;
  }
  std::vector<std::filesystem::path> files;
  std::error_code ec;
  for (auto &f : std::filesystem::directory_iterator(argv[1], ec))
    if (f.path().extension() == ".dxil" || f.path().extension() == ".dxbc")
      files.push_back(f.path());
  if (ec) {
    fprintf(stderr, "dxil-translate: can't read %s: %s\n", argv[1], ec.message().c_str());
    return 2;
  }
  std::sort(files.begin(), files.end());
  id<MTLDevice> device = MTLCreateSystemDefaultDevice();
  uint32_t ok = 0;
  double total_ms = 0, slowest_ms = 0;
  std::string slowest = "-";
  std::vector<VertexShader> vertex_shaders;
  std::vector<std::filesystem::path> deferred; // geometry shaders whose vertex shader may come later
  auto translate = [&](const std::filesystem::path &path, bool last_chance) {
    std::ifstream in(path, std::ios::binary);
    std::vector<char> bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    auto name = path.filename().string();
    auto start = std::chrono::steady_clock::now();
    Result r;
    @autoreleasepool {
      r = Translate(device, bytes, vertex_shaders, last_chance);
    }
    if (r.deferred) {
      deferred.push_back(path);
      return;
    }
    double ms = std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
    if (r.ok) {
      ok++;
      total_ms += ms;
      if (ms > slowest_ms)
        slowest_ms = ms, slowest = name;
      if (count_flags)
        printf("ok %s %s %.1f fence=%s barrier=%u grad=%u/%u nnan=%u ninf=%u cmp=%u reassoc=%u contract=%u arcp=%u vout=%u fdiv=%u/%u\n",
               name.c_str(), r.stage, ms, r.flags.fences.empty() ? "-" : r.flags.fences.c_str(), r.flags.barriers,
               r.flags.scaled_grads, r.flags.grads, r.flags.nnan, r.flags.ninf, r.flags.cmp, r.flags.reassoc, r.flags.contract, r.flags.arcp,
               r.flags.vout, r.flags.fast_fdivs, r.flags.fdivs);
      else
        printf("ok %s %s %.1f air=%016llx\n", name.c_str(), r.stage, ms, (unsigned long long)r.air);
    } else {
      printf("fail %s %s\n", name.c_str(), r.reason.c_str());
    }
    fflush(stdout);
  };
  for (auto &path : files)
    translate(path, false);
  for (auto &path : std::vector(deferred))
    translate(path, true);
  printf("summary %u/%zu ok, mean %.1f ms, slowest %.1f ms %s\n", ok, files.size(), ok ? total_ms / ok : 0.0, slowest_ms,
         slowest.c_str());
  return ok == files.size() && !files.empty() ? 0 : 1;
}
