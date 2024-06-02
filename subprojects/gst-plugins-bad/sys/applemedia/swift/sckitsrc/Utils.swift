import Dispatch

/// Our macro from C is not supported unfortunately, gotta do this manually
let GST_SECOND = 1_000_000_000 as UInt64

class BlockingTask {
  let semaphore = DispatchSemaphore(value: 0)
  init(block: @escaping () async throws -> Void) {
    Task {
      defer { semaphore.signal() }
      try await block()
    }
  }

  func wait() {
    semaphore.wait()
  }
}

class BlockingTaskWithResult<T> {
  let semaphore = DispatchSemaphore(value: 0)
  private var result: T?
  init(block: @escaping () async throws -> T) {
    Task {
      defer { semaphore.signal() }
      result = try await block()
    }
  }

  func get() -> T? {
    semaphore.wait()
    return result
  }
}
