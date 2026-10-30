// dxil-probe: can LLVM 15 read DXIL? (DXMT fork spec §6)
//   dxil-probe <file.dxil>...
//   dxil-probe -S <file.dxil>   prints the module as LLVM IR text
// One line per file:
//   ok <file> dxil=<major>.<minor> <stage>_<major>_<minor> entry=<names> ops=<dx.op callee>:<calls>,... (top 10)
//   fail <file> <reason, or LLVM's error>
// Exits 1 when any file fails.
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Metadata.h>
#include <llvm/IR/Module.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <map>
#include <optional>
#include <string>
#include <vector>

static uint32_t read32(const std::vector<char> &blob, size_t at) {
  uint32_t value = 0;
  if (at + 4 <= blob.size()) memcpy(&value, blob.data() + at, 4);
  return value;
}

// DXIL's shader kinds (DxilProgramHeader's high 16 bits).
static const char *const stages[] = {"ps", "vs", "gs", "hs", "ds", "cs", "lib", "raygen",
                                     "intersection", "anyhit", "closesthit", "miss", "callable", "ms", "as", "node"};

static bool print_ir = false;

// Fills `line` and returns "", or returns why the file can't be read.
static std::string probe(const std::vector<char> &blob, std::string &line) {
  if (blob.size() < 32 || memcmp(blob.data(), "DXBC", 4)) return "not a DXBC container";
  // The part table ends where the file does, whatever count the header claims.
  size_t part = 0, end = 0;
  for (uint64_t i = 0, parts = read32(blob, 28); i < parts && 36 + 4 * i <= blob.size(); i++) {
    size_t at = read32(blob, 32 + 4 * i);
    if (at + 8 <= blob.size() && !memcmp(blob.data() + at, "DXIL", 4)) {
      part = at + 8;
      end = std::min<uint64_t>(blob.size(), part + uint64_t(read32(blob, at + 4)));
      break;
    }
  }
  if (!part) return "no DXIL part (a DXBC shader)";
  // DxilProgramHeader: ProgramVersion, SizeInUint32, then DxilBitcodeHeader: "DXIL", DxilVersion, BitcodeOffset, BitcodeSize.
  if (part + 24 > end || memcmp(blob.data() + part + 8, "DXIL", 4)) return "bad DXIL program header";
  uint32_t program = read32(blob, part), version = read32(blob, part + 12);
  uint64_t start = part + 8 + uint64_t(read32(blob, part + 16)), size = read32(blob, part + 20);
  if (size < 4 || start + size > end) return "bitcode lies outside the DXIL part";

  llvm::LLVMContext context;
#if LLVM_VERSION_MAJOR < 17
  context.setOpaquePointers(false);  // airconv's contexts use typed pointers (AIR needs them)
#endif
  llvm::MemoryBufferRef buffer(llvm::StringRef(blob.data() + start, size), "dxil");
#if LLVM_VERSION_MAJOR < 17
  auto module = llvm::parseBitcodeFile(buffer, context);
#else
  // The newer reader refuses DXIL's data layout ("i8 must be 8-bit aligned"): hand it the default one instead.
  auto module = llvm::parseBitcodeFile(
      buffer, context, llvm::ParserCallbacks([](llvm::StringRef, llvm::StringRef) -> std::optional<std::string> {
        return std::string();
      }));
#endif
  if (!module) return "llvm: " + llvm::toString(module.takeError());
  if (print_ir) (*module)->print(llvm::outs(), nullptr);

  std::string entries;
  if (auto *points = (*module)->getNamedMetadata("dx.entryPoints"))
    for (auto *node : points->operands())
      if (node->getNumOperands() > 1)
        if (auto *name = llvm::dyn_cast_or_null<llvm::MDString>(node->getOperand(1).get()))
          entries += (entries.empty() ? "" : ",") + name->getString().str();
  std::map<std::string, int> calls;
  for (auto &function : **module)
    if (function.getName().substr(0, 6) == "dx.op.")
      for (auto *user : function.users())
        if (llvm::isa<llvm::CallInst>(user)) calls[function.getName().str().substr(6)]++;
  std::vector<std::pair<std::string, int>> top(calls.begin(), calls.end());
  std::sort(top.begin(), top.end(), [](auto &a, auto &b) { return a.second > b.second; });
  if (top.size() > 10) top.resize(10);
  std::string ops;
  for (auto &[name, count] : top) ops += (ops.empty() ? "" : ",") + name + ":" + std::to_string(count);

  unsigned kind = program >> 16;
  char head[128];
  snprintf(head, sizeof head, "dxil=%u.%u %s_%u_%u", version >> 8, version & 0xff,
           kind < sizeof stages / sizeof *stages ? stages[kind] : "unknown", (program >> 4) & 0xf, program & 0xf);
  line = std::string(head) + " entry=" + entries + " ops=" + ops;
  return "";
}

int main(int argc, char **argv) {
  if (argc < 2) return fprintf(stderr, "usage: dxil-probe [-S] <file.dxil>...\n"), 1;
  int failures = 0, first = 1;
  if (argc > 1 && std::string(argv[1]) == "-S") print_ir = true, first = 2;
  for (int i = first; i < argc; i++) {
    std::ifstream in(argv[i], std::ios::binary);
    std::vector<char> blob;
    if (in.is_open()) blob.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    std::string line, reason = in.is_open() ? probe(blob, line) : "can't read the file";
    if (reason.empty()) {
      printf("ok %s %s\n", argv[i], line.c_str());
    } else {
      printf("fail %s %s\n", argv[i], reason.c_str());
      failures++;
    }
  }
  return failures ? 1 : 0;
}
