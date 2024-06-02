import CGStreamer

@freestanding(expression)
public macro gstError(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstErrorMacro")

@freestanding(expression)
public macro gstWarning(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstWarningMacro")

@freestanding(expression)
public macro gstFixme(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstFixmeMacro")

@freestanding(expression)
public macro gstInfo(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstInfoMacro")

@freestanding(expression)
public macro gstDebug(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstDebugMacro")

@freestanding(expression)
public macro gstLog(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstLogMacro")

@freestanding(expression)
public macro gstTrace(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstTraceMacro")

@freestanding(expression)
public macro gstMemdump(_ cat: UnsafeMutablePointer<GstDebugCategory>, _: String) =
  #externalMacro(module: "gstSwiftMacros", type: "GstMemdumpMacro")

@freestanding(expression)
public macro gstErrorObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstErrorObjectMacro")

@freestanding(expression)
public macro gstWarningObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstWarningObjectMacro")

@freestanding(expression)
public macro gstFixmeObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstFixmeObjectMacro")

@freestanding(expression)
public macro gstInfoObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstInfoObjectMacro")

@freestanding(expression)
public macro gstDebugObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstDebugObjectMacro")

@freestanding(expression)
public macro gstLogObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstLogObjectMacro")

@freestanding(expression)
public macro gstTraceObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstTraceObjectMacro")

@freestanding(expression)
public macro gstMemdumpObject(
  _ cat: UnsafeMutablePointer<GstDebugCategory>, _ obj: Any, _: String
) =
  #externalMacro(module: "gstSwiftMacros", type: "GstMemdumpObjectMacro")
