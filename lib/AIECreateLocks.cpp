// (c) Copyright 2019 Xilinx Inc. All Rights Reserved.

#include "mlir/IR/Attributes.h"
#include "mlir/IR/PatternMatch.h"
#include "mlir/IR/Location.h"
#include "mlir/Pass/Pass.h"
#include "mlir/Transforms/DialectConversion.h"
#include "mlir/Translation.h"
#include "AIEDialect.h"
#include "AIETokenAnalysis.h"

#define DEBUG_TYPE "aie-create-locks"
using namespace mlir;
using namespace xilinx;
using namespace xilinx::AIE;

struct Token2LockLowering : public OpConversionPattern<UseTokenOp> {
  using OpConversionPattern<UseTokenOp>::OpConversionPattern;
  ModuleOp &module;
  DenseMap<Operation *, std::vector<std::pair<Value, int>>> &acqLocks;
  DenseMap<Operation *, std::vector<std::pair<Value, int>>> &relLocks;
  DenseMap<std::pair<Operation *, Operation *>, std::pair<Value, int>> &lockChains;

  Token2LockLowering(MLIRContext *context, ModuleOp &m,
    DenseMap<Operation *, std::vector<std::pair<Value, int>>> &acqLocks,
    DenseMap<Operation *, std::vector<std::pair<Value, int>>> &relLocks,
    DenseMap<std::pair<Operation *, Operation *>, std::pair<Value, int>> &lockChains,
    PatternBenefit benefit = 1
  ) : OpConversionPattern<UseTokenOp>(context, benefit),
    module(m), lockChains(lockChains), acqLocks(acqLocks), relLocks(relLocks) {}

  LogicalResult matchAndRewrite(UseTokenOp op, ArrayRef<Value> operands,
                                ConversionPatternRewriter &rewriter) const override {
    Operation *Op = op.getOperation();
    Operation *parentOp = op.getParentOp();

    int srcCol, srcRow;
    bool IsParentMemOp = false;
    if (CoreOp core = dyn_cast<CoreOp>(parentOp)) {
      srcCol = core.colIndex();
      srcRow = core.rowIndex();
    } else if (MemOp mem = dyn_cast<MemOp>(parentOp)) {
      srcCol = mem.colIndex();
      srcRow = mem.rowIndex();
      IsParentMemOp = true;
    } else {
      llvm_unreachable("A parent operation of UseTokenOp must be either CoreOp or MemOp");
    }

    // Default timeout value
    int timeout = 0;

    if (op.acquire()) {
      // Acquire lock from pair
      LLVM_DEBUG(llvm::dbgs() << "Replacing Acquire: " << op << "\n");
      for(auto acqLock : acqLocks[op]) {
        Value lockFromPair    = acqLock.first;
        int lockValueFromPair = acqLock.second;
        rewriter.create<UseLockOp>(op.getLoc(), lockFromPair, lockValueFromPair,
                                   LockAction::Acquire, timeout);
        LLVM_DEBUG(llvm::dbgs() << "Acquire from pair " << lockFromPair << " with " << lockValueFromPair << "\n");
      }

      // Acquire lock from chain
      for (auto map : lockChains) {
        Value lockFromChain    = map.second.first;
        int lockValueFromChain = map.second.second;
        Operation *acqOp = map.first.second;
        if (acqOp != Op)
          continue;

        rewriter.create<UseLockOp>(op.getLoc(), lockFromChain, lockValueFromChain,
                                   LockAction::Acquire, timeout);
        LLVM_DEBUG(llvm::dbgs() << "Acquire from chain " << lockFromChain << " with " << lockValueFromChain << "\n");
      }
    } else if (op.release()) {
      // Release lock from pair
      LLVM_DEBUG(llvm::dbgs() << "Replacing Release: " << op << "\n");
      for(auto relLock : relLocks[op]) {
        Value lockFromPair    = relLock.first;
        int lockValueFromPair = relLock.second;
        rewriter.create<UseLockOp>(op.getLoc(), lockFromPair, lockValueFromPair,
                                   LockAction::Release, timeout);
        LLVM_DEBUG(llvm::dbgs() << "Release from pair " << lockFromPair << " with " << lockValueFromPair << "\n");
      }

      // Release lock from chain
      for (auto map : lockChains) {
        Value lockFromChain    = map.second.first;
        int lockValueFromChain = map.second.second;
        Operation *relOp = map.first.first;
        if (relOp != Op)
          continue;

        rewriter.create<UseLockOp>(op.getLoc(), lockFromChain, lockValueFromChain,
                                   LockAction::Release, timeout);
        LLVM_DEBUG(llvm::dbgs() << "Release from chain " << lockFromChain << " with " << lockValueFromChain << "\n");
     }
    }

    rewriter.eraseOp(Op);

    return success();
  }
};

static int getLockID(DenseMap<std::pair<Operation *, int>, int> &locks, Operation *tileOp) {

  for (unsigned i = 0; i < 16; i++) {
    int usageCnt = locks[std::make_pair(tileOp, i)];
    if (usageCnt == 0) {
      locks[std::make_pair(tileOp, i)] = 1;
      return i;
    }
  }

  return -1;
}

struct AIECreateLocksPass : public PassWrapper<AIECreateLocksPass, OperationPass<ModuleOp>> {
  void runOnOperation() override {

    ModuleOp m = getOperation();
    OpBuilder builder(m.getBody()->getTerminator());

    TokenAnalysis TA(m);
    TA.runAnalysis();
    TA.print(llvm::dbgs());
    DenseMap<StringRef, SmallVector<Operation *, 4>> tokenAcqMap(TA.getTokenAcqMap());
    DenseMap<StringRef, SmallVector<Operation *, 4>> tokenRelMap(TA.getTokenRelMap());
    SmallVector<std::pair<Operation *, Operation *>, 4> tokenChains(TA.getTokenChains());
    SmallVector<std::pair<Operation *, Operation *>, 4> tokenPairs(TA.getTokenPairs());
    DenseMap<std::pair<int, int>, Operation *> tiles(TA.getTiles());

    DenseMap<std::pair<Operation *, int>, int> locks;
    DenseMap<std::pair<Operation *, Operation *>, std::pair<Value, int>> lockChains;
    DenseMap<Operation *, std::vector<std::pair<Value, int>>> acqLocks;
    DenseMap<Operation *, std::vector<std::pair<Value, int>>> relLocks;

    for (auto map : tokenChains) {
      Operation *release = map.first;
      Operation *acquire = map.second;

      Operation *relUser = TA.getTokenUserOp(release);
      Operation *acqUser = TA.getTokenUserOp(acquire);
      bool IsRelUserCore = isa<CoreOp>(relUser);
      bool IsAcqUserCore = isa<CoreOp>(acqUser);
      std::pair<int, int> relUserCoord = TA.getCoord(relUser);
      std::pair<int, int> acqUserCoord = TA.getCoord(acqUser);

      Operation *tileOp = TA.getShareableTileOp(relUser, acqUser);

      LLVM_DEBUG(
        llvm::dbgs() << "\n=== CHECKING TOKEN CHAIN ===\n";
      release->print(llvm::dbgs());
      if(IsRelUserCore) llvm::dbgs() << " @Core";
      else llvm::dbgs() << " @DMA";
      llvm::dbgs() << " (" << relUserCoord.first << ", "
                                        << relUserCoord.second << ")" << '\n';
      acquire->print(llvm::dbgs());
      if(IsAcqUserCore) llvm::dbgs() << " @Core";
      else llvm::dbgs() << " @DMA";
      llvm::dbgs() << " (" << acqUserCoord.first << ", "
                                        << acqUserCoord.second << ")" << '\n';
      );

      // ignore chain that involves a MemOp (DMA) user and CoreOp user and they don't have
      // a shareable tile. This might be caused by MemcpyOp lowering -- there are two MemOps
      // that use the same token and the same lock action + value. Therefore, TokenAnalysis
      // accidentally chains one MemOp to a Core (from the MemcpyOp relationship) that
      // does not have memory affinity with it
      // TODO: verify if it is actually safe to ignore this case
      if (!tileOp && ((!IsRelUserCore && IsAcqUserCore) || (!IsAcqUserCore && IsRelUserCore)))
        continue;
      assert(tileOp && "Sorry, the lock users of this chain do not have a common lock");

      TileOp tile = dyn_cast<TileOp>(tileOp);
      int lockID = getLockID(locks, tileOp);
      assert(lockID >= 0 && "No more locks to allocate!");
      LLVM_DEBUG(llvm::dbgs() << "Shared tile \n"; tileOp->print(llvm::dbgs()));
      LLVM_DEBUG(llvm::dbgs() << " LockID: " << lockID << '\n');
      builder.setInsertionPointAfter((Value)tile);
      LockOp lock = builder.create<LockOp>(builder.getUnknownLoc(), tile, lockID);

      lockChains[std::make_pair(release, acquire)] = std::make_pair(lock, 1);

      for (auto pair : tokenPairs) {
        Operation *acqFromPair = pair.first;
        Operation *relFromPair = pair.second;

        if (relFromPair == release)
          acqLocks[acqFromPair].push_back(std::make_pair(lock, 0));

        if (acqFromPair == acquire)
          relLocks[relFromPair].push_back(std::make_pair(lock, 0));
      }
    }

    ConversionTarget target(getContext());
    target.addLegalOp<UseLockOp>();

    OwningRewritePatternList patterns;
    patterns.insert<Token2LockLowering>(m.getContext(), m, acqLocks, relLocks, lockChains);

    if (failed(applyPartialConversion(m, target, patterns)))
      signalPassFailure();
  }
};

void xilinx::AIE::registerAIECreateLocksPass() {
    PassRegistration<AIECreateLocksPass>(
      "aie-create-locks",
      "Generate physical lock ops from logical lock (token) ops");
}
