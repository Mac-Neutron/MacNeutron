// air15: LLVM 15's typed-pointer view of AIR metallibs, the oracle side of DXMTRS step 4W (the AIR writer for LLVM 23).
//   air15 extract <in.metallib> <out.bc>      the bytes at the header's BitcodeOffset and BitcodeSize
//   air15 dis <in.metallib|in.bc> <out|->     typed Module::print, empty module identifier (no ModuleID line)
//   air15 types <in.metallib|in.bc> <out|->   the types file (grammar below)
//   air15 verify <in.metallib|in.bc>          prints "ok", or "fail <reason>" and exits 1
//   air15 pack <orig.metallib> <new.bc> <out.metallib>   orig's container around new.bc (FileSize, BitcodeSize, HASH, MDSZ)
// Every subcommand but verify's "fail" line exits 0 on success and 1 with the reason on stderr. A file that is not a
// metallib (no "MTLB" magic) is taken as bare bitcode, Darwin wrapper or not.
//
// The types file lists the typed types that opaque pointers lose. Types are LLVM 15's Type::print (an identified struct
// as %name; an unnamed one as Module::print numbers it, %0, %1, …), names are Module::print's identifier forms (@x,
// @"…\XX…", %x). One line each, in this order:
//   s <%struct> <typed literal body>   every identified struct with a pointer inside
//   g <@global> <typed value type>     every global whose value type has a pointer
//   f <@function> <typed function type>   every function whose type has a pointer
//   then, per function in module order:
//   c <@function> <n>                  instruction count, for a function with a body
//   i <@function> <k> <typed type>     the k-th instruction (0-based, function order) when its type has a pointer
//
// The container layout is src/airconv/metallib_writer.cpp's (DXMTRS da6ed2d): an 88-byte MTLBHeader, then at
// FunctionListOffset a u32 function count, a u32 FunctionListSize and per function tags <4cc><u16 size><payload> up to
// a bare ENDT. pack rewrites each function's HASH (SHA-256 of the bitcode) and MDSZ (its size).
#include <CommonCrypto/CommonDigest.h>
#include <llvm/Bitcode/BitcodeReader.h>
#include <llvm/IR/Instructions.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/LLVMContext.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/TypeFinder.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/Error.h>
#include <llvm/Support/MemoryBuffer.h>
#include <llvm/Support/raw_ostream.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <iterator>
#include <string>

using namespace llvm;

namespace {

struct Failure {
  std::string why;
};

[[noreturn]] void fail(std::string why) { throw Failure{std::move(why)}; }

std::string readFile(const char *path) {
  std::ifstream in(path, std::ios::binary);
  if (!in.is_open()) fail(std::string("can't read ") + path);
  return std::string(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
}

void writeFile(const char *path, const std::string &data) {
  if (!strcmp(path, "-")) {
    if (fwrite(data.data(), 1, data.size(), stdout) != data.size()) fail("can't write to stdout");
    return;
  }
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  out.write(data.data(), std::streamsize(data.size()));
  out.flush();
  if (!out) fail(std::string("can't write ") + path);
}

uint64_t rd(const std::string &b, size_t at, size_t n) {
  if (at > b.size() || n > b.size() - at) fail("container: read past the end of the file");
  uint64_t v = 0;
  memcpy(&v, b.data() + at, n);  // little endian host, as the writer's
  return v;
}

void wr(std::string &b, size_t at, uint64_t v, size_t n) {
  if (at > b.size() || n > b.size() - at) fail("container: write past the end of the file");
  memcpy(&b[at], &v, n);
}

constexpr size_t kHeader = 88;
constexpr size_t kFileSize = 16, kListOffset = 24, kPublicOffset = 40, kBitcodeOffset = 72, kBitcodeSize = 80;

bool isMetallib(const std::string &b) { return b.size() >= 4 && !memcmp(b.data(), "MTLB", 4); }

struct Slice {
  size_t offset, size;
};

Slice bitcodeSlice(const std::string &b) {
  if (b.size() < kHeader) fail("container: shorter than the 88-byte header");
  uint64_t off = rd(b, kBitcodeOffset, 8), size = rd(b, kBitcodeSize, 8);
  if (off > b.size() || size > b.size() - off) fail("container: the bitcode lies outside the file");
  return {size_t(off), size_t(size)};
}

// The module's bitcode: the metallib's slice, or the file itself.
std::string bitcodeOf(const char *path) {
  std::string b = readFile(path);
  if (!isMetallib(b)) return b;
  Slice s = bitcodeSlice(b);
  return b.substr(s.offset, s.size);
}

std::unique_ptr<Module> loadModule(LLVMContext &ctx, const std::string &bc) {
  ctx.setOpaquePointers(false);
  auto m = parseBitcodeFile(MemoryBufferRef(bc, ""), ctx);
  if (!m) fail("llvm: " + toString(m.takeError()));
  (*m)->setModuleIdentifier("");
  return std::move(*m);
}

std::string typeText(Type *t) {  // NoDetails: an identified struct is %name, not its definition
  std::string s;
  raw_string_ostream os(s);
  t->print(os, false, true);
  return os.str();
}

std::string nameText(const Value &v) {
  std::string s;
  raw_string_ostream os(s);
  v.printAsOperand(os, false);
  return os.str();
}

bool hasPointer(Type *t, SmallPtrSetImpl<Type *> &seen) {
  if (t->isPointerTy()) return true;
  if (!seen.insert(t).second) return false;
  if (auto *s = dyn_cast<StructType>(t)) {
    for (Type *e : s->elements())
      if (hasPointer(e, seen)) return true;
    return false;
  }
  if (auto *f = dyn_cast<FunctionType>(t)) {
    if (hasPointer(f->getReturnType(), seen)) return true;
    for (Type *p : f->params())
      if (hasPointer(p, seen)) return true;
    return false;
  }
  if (t->isArrayTy() || t->isVectorTy()) return hasPointer(t->getContainedType(0), seen);
  return false;
}

bool hasPointer(Type *t) {
  SmallPtrSet<Type *, 8> seen;
  return hasPointer(t, seen);
}

std::string typesFile(Module &m) {
  std::string out;
  for (StructType *s : m.getIdentifiedStructTypes()) {
    if (s->isOpaque() || !hasPointer(s)) continue;
    std::string full, name = typeText(s);
    raw_string_ostream os(full);
    s->print(os);  // "%name = type { … }", the same printer as Module::print
    os.flush();
    std::string head = name + " = type ";
    if (full.compare(0, head.size(), head) != 0) fail("types: unexpected struct print for " + name);
    out += "s " + name + " " + full.substr(head.size()) + "\n";
  }
  for (GlobalVariable &g : m.globals())
    if (hasPointer(g.getValueType())) out += "g " + nameText(g) + " " + typeText(g.getValueType()) + "\n";
  for (Function &f : m)
    if (hasPointer(f.getFunctionType())) out += "f " + nameText(f) + " " + typeText(f.getFunctionType()) + "\n";
  for (Function &f : m) {
    if (f.isDeclaration()) continue;
    std::string name = nameText(f);
    unsigned count = 0;
    for (BasicBlock &bb : f) count += unsigned(bb.size());
    out += "c " + name + " " + std::to_string(count) + "\n";
    unsigned k = 0;
    for (BasicBlock &bb : f)
      for (Instruction &i : bb) {
        if (hasPointer(i.getType())) out += "i " + name + " " + std::to_string(k) + " " + typeText(i.getType()) + "\n";
        k++;
      }
  }
  // Type::print without the module prints an unnamed identified struct as %"type 0x<address>"; Module::print numbers
  // them in TypeFinder order (AsmWriter's TypePrinting::incorporateTypes), and the types file takes that name.
  TypeFinder found;
  found.run(m, false);
  unsigned number = 0;
  for (StructType *s : found) {
    if (s->isLiteral() || s->hasName()) continue;
    std::string addr;
    {
      raw_string_ostream os(addr);
      os << "%\"type " << s << '"';
    }
    std::string num = "%" + std::to_string(number++);
    for (size_t at = out.find(addr); at != std::string::npos; at = out.find(addr, at + num.size()))
      out.replace(at, addr.size(), num);
  }
  return out;
}

std::string oneLine(const std::string &s) {  // the verifier's text, whitespace runs as one space
  std::string r;
  for (char c : s) {
    bool space = c == '\n' || c == '\t' || c == ' ';
    if (space && (r.empty() || r.back() == ' ')) continue;
    r += space ? ' ' : c;
  }
  while (!r.empty() && r.back() == ' ') r.pop_back();
  return r;
}

// "" when the module passes, else why not. Beyond verifyModule, two things LLVM 15 leaves unchecked: an atomic's
// pointer operand must point to its value type (the reader takes the values with getValueTypePair, and the verifier
// checks the pointee for store alone), and a "llvm." declaration must be an intrinsic LLVM 15 knows.
std::string verifyText(Module &m) {
  std::string v;
  raw_string_ostream vos(v);
  if (verifyModule(m, &vos)) return oneLine(vos.str());
  auto pointee = [](Value *p) { return cast<PointerType>(p->getType())->getNonOpaquePointerElementType(); };
  for (Function &f : m) {
    if (f.getName().startswith("llvm.") && f.getIntrinsicID() == Intrinsic::not_intrinsic)
      return "unknown intrinsic " + nameText(f) + " (LLVM 15 reads it as a plain function)";
    for (BasicBlock &bb : f)
      for (Instruction &i : bb) {
        Type *want = nullptr, *got = nullptr;
        const char *what = nullptr;
        if (auto *x = dyn_cast<AtomicCmpXchgInst>(&i)) {
          what = "cmpxchg", got = pointee(x->getPointerOperand());
          if (got != x->getCompareOperand()->getType()) want = x->getCompareOperand()->getType();
          else if (got != x->getNewValOperand()->getType()) want = x->getNewValOperand()->getType();
        } else if (auto *x = dyn_cast<AtomicRMWInst>(&i)) {
          what = "atomicrmw", got = pointee(x->getPointerOperand());
          if (got != x->getValOperand()->getType()) want = x->getValOperand()->getType();
        }
        if (want) return std::string(what) + ": pointer operand points to " + typeText(got) + ", not " +
                         typeText(want) + " (in " + nameText(f) + ")";
      }
  }
  return "";
}

// Rewrites each function's HASH and MDSZ in place.
void patchFunctions(std::string &b, const uint8_t hash[CC_SHA256_DIGEST_LENGTH], uint64_t bitcodeSize) {
  size_t at = size_t(rd(b, kListOffset, 8)), limit = size_t(rd(b, kPublicOffset, 8));
  if (limit > b.size() || at + 8 > limit) fail("container: bad function list");
  uint32_t count = uint32_t(rd(b, at, 4));
  at += 8;  // the count and FunctionListSize
  for (uint32_t f = 0; f < count; f++) {
    for (;;) {
      if (at + 4 > limit) fail("container: a function's tags run past the function list");
      if (!memcmp(b.data() + at, "ENDT", 4)) {
        at += 4;
        break;
      }
      if (at + 6 > limit) fail("container: a tag runs past the function list");
      size_t size = size_t(rd(b, at + 4, 2));
      if (at + 6 + size > limit) fail("container: a tag's payload runs past the function list");
      if (!memcmp(b.data() + at, "HASH", 4)) {
        if (size != CC_SHA256_DIGEST_LENGTH) fail("container: HASH is not 32 bytes");
        memcpy(&b[at + 6], hash, size);
      } else if (!memcmp(b.data() + at, "MDSZ", 4)) {
        if (size != 8) fail("container: MDSZ is not 8 bytes");
        wr(b, at + 6, bitcodeSize, 8);
      }
      at += 6 + size;
    }
  }
}

std::string pack(const std::string &orig, const std::string &bc) {
  if (!isMetallib(orig)) fail("pack: the first argument is not a metallib");
  Slice s = bitcodeSlice(orig);
  std::string out = orig.substr(0, s.offset) + bc + orig.substr(s.offset + s.size);
  uint8_t hash[CC_SHA256_DIGEST_LENGTH];
  CC_SHA256(bc.data(), CC_LONG(bc.size()), hash);
  patchFunctions(out, hash, bc.size());
  wr(out, kBitcodeSize, bc.size(), 8);
  wr(out, kFileSize, out.size(), 8);
  return out;
}

int run(int argc, char **argv) {
  std::string cmd = argc > 1 ? argv[1] : "";
  if (cmd == "extract" && argc == 4) {
    writeFile(argv[3], bitcodeOf(argv[2]));
  } else if ((cmd == "dis" || cmd == "types") && argc == 4) {
    LLVMContext ctx;
    auto m = loadModule(ctx, bitcodeOf(argv[2]));
    std::string text;
    if (cmd == "types") {
      text = typesFile(*m);
    } else {
      raw_string_ostream os(text);
      m->print(os, nullptr);
    }
    writeFile(argv[3], text);
  } else if (cmd == "verify" && argc == 3) {
    std::string why;
    try {
      LLVMContext ctx;
      why = verifyText(*loadModule(ctx, bitcodeOf(argv[2])));
    } catch (Failure &f) {
      why = f.why;
    }
    if (why.empty()) return puts("ok"), 0;
    printf("fail %s\n", why.c_str());
    return 1;
  } else if (cmd == "pack" && argc == 5) {
    writeFile(argv[4], pack(readFile(argv[2]), readFile(argv[3])));
  } else {
    fail("usage: air15 extract <in.metallib> <out.bc> | dis|types <in> <out|-> | verify <in> | "
         "pack <orig.metallib> <new.bc> <out.metallib>");
  }
  return 0;
}

}  // namespace

int main(int argc, char **argv) {
  try {
    return run(argc, argv);
  } catch (Failure &f) {
    fprintf(stderr, "air15: %s\n", f.why.c_str());
    return 1;
  }
}
