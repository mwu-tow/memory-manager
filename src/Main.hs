{-# LANGUAGE ForeignFunctionInterface #-}
{-# LANGUAGE MagicHash #-}
{-# LANGUAGE BangPatterns #-}
{-# LANGUAGE UnliftedFFITypes #-}
{-# LANGUAGE DeriveGeneric, DeriveAnyClass #-}
{-# LANGUAGE ScopedTypeVariables #-}

module Main where

import Criterion.Main
import Foreign (Ptr)
import Foreign.C (CDouble(..), CSize(..))
import Foreign.Marshal.Alloc (mallocBytes, free)
import GHC.Generics (Generic)
import Control.DeepSeq (NFData)

newtype MemoryManager = MemoryManager (Ptr ()) deriving (Eq, Ord, Show, Generic, NFData)
type Item = Ptr ()

foreign import ccall unsafe "newManager"    newManager    :: CSize -> CSize -> IO MemoryManager
foreign import ccall unsafe "deleteManager" deleteManager :: MemoryManager -> IO ()
foreign import ccall unsafe "newItem"       newItem       :: MemoryManager -> IO Item
foreign import ccall unsafe "deleteItem"    deleteItem    :: MemoryManager -> Item -> IO ()
-------------------------------------------------------------------------------------------
foreign import ccall unsafe "benchmark"           benchmark           :: CSize -> CSize -> IO()
foreign import ccall unsafe "randomizedBenchmark" randomizedBenchmark :: CSize -> CSize -> CDouble -> IO()
foreign import ccall unsafe "justReturn"          justReturn          :: CSize -> IO (Ptr ())

-- Benchmarks IO action that uses MemoryManager
benchmarkWithManager :: NFData a
                     => String -> (MemoryManager -> IO a) -> CSize -> CSize
                     -> Benchmark
benchmarkWithManager name action itemSize blockSize = envWithCleanup
    (newManager itemSize blockSize)
    deleteManager
    (\mgr -> bench name $ nfIO (action mgr))

testAllocFreePairsCpp :: CSize -> CSize -> Benchmark
testAllocFreePairsCpp = benchmarkWithManager
    "FFI: C++ manager (new >>= delete)"
    (\mgr -> newItem mgr >>= deleteItem mgr)

main :: IO ()
main = do 
    defaultMain 
        [ testAllocFreePairsCpp 64 1024
        , benchmarkWithManager "FFI: C++ manager (new)" newItem 64 1024
        , bench "FFI: trivial call" $ nfIO $ justReturn 5 -- TODO allocates much memory, so for higher iteration count there will be slow downs
        , bench "Foreign.Marshal.Alloc" $ nfIO $ (mallocBytes 64 >>= free)
        , bench "C++: randomized pattern" $ nfIO $ randomizedBenchmark 10000000 64 0.7
        ]
    -- benchmark 10000000 64
