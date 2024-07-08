# Machine Learning Based Analytics

Analytics refer to the process of extracting information from the content of the
media (or medias). The analysis can be spatial only, for example, image analysis, or
temporal only, like sound detection, or even spatio-temporal tracking or action recognition,
multi-modal image+sound to detect a environment or behaviour. There's also
scenarios where the results of the analysis is used as the input, with or without an
additional media. This design aim is to support ML-based analytics and CV
analytics and offer a way to bridge both techniques.

## Vision

With this design we aim at allowing GStreamer application developers to develop
analytics pipeline easily while taking full advantage of the acceleration
available on the platform where they deploy. The effort of moving the analytic
pipeline to a different platform should be minimal.

## Refinement Using Analytics Pipeline

Similarly to content agnostic media processing (ex. Scaling, color-space change,
serialization, ...), this design promote re-usability and simplicity by allowing
the composition of complex analytics pipelines from simple dedicated analytics
elements that complement each other.

### Example
Simple hypothetical example of an analytic pipeline.

```
+---------+    +----------+    +---------------+    +----------------+
| v4l2src |    | video    |    | onnxinference |    | tensor-decoder |
|         |    |  convert |    |               |    |                |
|        src-sink  scale src-sink1           src1-sink              src---
|         |    |(pre-proc)|    | (analysis)    |    | (post-proc)    |   /
+---------+    +----------+    +---------------+    +----------------+  /
                                                                       /
----------------------------------------------------------------------
|  +-------------+    +------+
|  | Analytic-   |    | sink |
|  |  overlay    |    |      |
-sink           src-sink     |
   | (analysis   |    |      |
   |  -results   |    +------+
   |  -consumer) |
   +-------------+

```

## Supporting Neural Network Inference

There are multiple frameworks supporting neural network inference. Those can be
described more generally as computing graphs, as they are generally not limited
to NN inference applications. Existing NN inference or computing graph frameworks,
like ONNX-Runtime, are encapsulated into a GstElement/Filter. The inference element loads
a model, describing the computing graph, specified by a property. The model
expects inputs in a specific format and produce outputs in specific
format. Depending on the model format, input/output formats can be extracted
from the model, like with ONNX, but it is not always the case.

### Inference Element
Inference elements are an encapsulation of an NN Inference framework. Therefore
they are specific to a framework, like ONNX-Runtime or TensorFlow-Lite.
Other inference elements can be added.

### Inference Input(s)
The input format is defined by the model. Using the model input format the
inference element can constrain its sinkpad(s) capabilities. Note, because tensors
are very generic, the term also encapsulates images/frames, and the term input tensor is
also used to describe inference input.

### Inference Output(s)
Output(s) of the inference are tensors and their format are also dictated by the
model. Analysis results are generally encoded in the output tensor in a way that
is specific to the model. Even models that target the same type of analysis
encode results in different ways.

### Models Format Not Describing Inputs/Outputs Tensor Format
With some models, the input/output tensor format are not described. In
this context, it's the responsibility of the analytics pipeline to push input
tensors with the correct format into the inference process. In this context,
the inference element designer is left with two choices: supporting a model manifest
where inputs/outputs are described or leaving the constraining/fixing the
inputs/outputs to analytics pipeline designer who can use caps filters to
constrain inputs/outputs of the model.

### Tensor Decoders
In order to preserve the generality of the inference element, tensor decoding is
omitted from the inference element and left to specialized elements that have a
specific task of decoding tensor from a specific model. Additionally
tensor decoding does not depend on a specific NN framework or inference element,
this allow reusing the tensor decoders with a same model used with a
different inference element. For example, a YOLOv3 tensor decoder can used to
decode tensor from inference using YOLOv3 model with an element encapsulating
ONNX or TFLite. Note that a tensor decoder can handle multiple tensors that have
similar encoding.

### Tensor
N-dimensional vector.

#### Tensor Type Identifier
This is an identifier, string or quark, that uniquely identifies a tensor type. The
tensor type describes the specific format used to encode analysis result in
memory. This identifier is used by tensor-decoders to know if they can handle
the decoding of a tensor. For this reason, from an implementation perspective,
the tensor decoder is the ideal location to store the tensor type identifier as the code
is already model specific. Since the tensor decoder is by design specific to a
model, no generality is lost by storing it the tensor type identifier.

#### Tensor Datatype
This is the primitive type used to store tensor-data. Like `int8`,
`uint8`, `float16`, `float32`, ...

#### Tensor Dimension Cardinality

Number of dimensions in the tensor.

#### Tensor Dimension

Tensor shape.

- [a], 1-dimensional vector
- [a x b], 2-dimensional vector
- [a x b x c], 3-dimensional vector
- [a x b x ... x n], N-dimensional vector

### Tensor Decoders Need to Recognize Tensor(s) They Can Handle

As mention before, tensor decoders need to be able to recognize tensor(s) they can
handle. It's important to keep in mind that multiple tensors can be attached to
a buffer, when tensors are transported as a meta. It could be easy to
believe that tensor's (cardinality + dimension + data type) is sufficient to
recognize a specific tensor format but we need to remember that analysis results
are encoded into the tensor and retrieve analysis results require a decoding
process specific to the model. In other words a tensor A:{cardinality:3,
dimension: 100 x 5, datatype:int8) and a tensor B:{cardinality:3, 100 x 5,
datatype:int8) can have completely different meaning.

A could be: (Object-detection where each candidate is encoded with (top-left)
coordinates, width, height and object location confidence level)

```
0 : [ x1, y1, w, h, location confidence]
1 : [ x1, y1, w, h, location confidence]
...
99: [ x1, y1, w, h, location confidence]
```

B could be: (Object-detection where each candidate is encoded with (top-left)
coordinates, (bottom-right) coordinate and object class confidence level)
```
0 : [ x1, y1, x2, y2, class confidence]
1 : [ x1, y1, x2, y2, class confidence]
...
99: [ x1, y1, x2, y2, class confidence]
```
We can see that even if A and B have same (cardinality, dimension, data type) a
tensor-decoder expecting A and decoding B would wrong.

In general, for high cardinality tensors, the risk of having two tensors with same
(cardinality + dimension + data type) is low, but if we think of low cardinality
tensors typical of classification (1 x C), we can see that the risk is much
higher. For this reason, we believe it's not sufficient for tensor-decoder to
only rely on (cardinality + dimension + data type) to identify tensor it can
handle.

#### A Tensor Decoder's Second Job: Non-Maximum Suppression (NMS)

The main functionality of Tensor-Decoders is to extract analytics-results from tensors,
but in addition to decoding tensors, in general a second phase of post-processing
is handled by tensor-decoder. This post-processing phase is called non-maximum
suppression (NMS). A simplest example of NMS, is with classification. For every
input, the classification model will produce a probability for potential class.
In general, we're mostly interested in the most probable class or few most
probable class, but there's little value in transport all classes
probability. In addition to keeping only most the probable class (or classes), we
often want the probability to be above a certain threshold, otherwise we're
not interested in the result. Because a significant portion of analytics results
from the inference process don't have much value, we want to filter them out
as early as possible. Since analytics results are only available after tensor
decoding, the tensor decoder is tasked with this type filtering (NMS). The same
concept exists for object detection, where NMS generally involves calculating
the intersection-of-union (IoU) in combination with location and class probability.
Because ML-based analytics are probabilistic by nature, they generally need a form of
NMS post-processing.

#### Handling Multiple Tensors Simultaneously In A Tensor Decoder
Sometimes, it is needed or more efficient to have a tensor decoder handle
multiple tensors simultaneously. In some cases, the tensors are complementary and a
tensor decoder needs to have both tensors to decode analytics result. In other
cases, it's just more efficient to do it simultaneously because of the
tensor-decoder's second job doing NMS. Let's consider YOLOv3, where 3 output tensors are
produced for each input. One tensor represents detection of small objects, a second
tensor medium size objects and a third tensor large size objects. In this context,
it's beneficial to have the tensor decoder decode the 3 tensors simultaneously to
perform the NMS on all the results, otherwise analytics results with low value
would remain in the system for longer. This has implications for the negotiation
of tensor decoders, that will be expanded on in  the section dedicated to tensor decoder
negotiation.

### Why Interpreting (decoding) Tensors
As we described above, tensors contain information and are used to store analytics
results. The analytics results are encoded in a model specific way into the
tensor and unless their consumers, processes making use of analytics-results, are
also model specific, they need to be decoded. Deciding if the analytics pipeline
will have elements producing and consuming tensor directly into their encoded
form, or if a tensor-decoding process will done between tensor production and
consumption, is a design decision that involve compromise between re-usability
and performance. As an example, an object detection overlay element would need to
be model specific to directly consume tensor. Therefore, it would need to be
re-written for any object-detection model using a different encoding scheme, but
if the only goal of the analytics pipeline is to do this overlay, it would
probably be the most efficient implementation. Another aspect in favour of
interpreting tensor is that we can have multiple consumers of the analytics
results, and if the tensor decoding is left to the consumers themselves, it implies
decoding the same tensor multiple times. However, we can think of two models
specifically designed to work together where the output of one model becomes the
input of the downstream model. In this context the downstream model is not
re-usable without the upstream model but they bypass the need for
tensor-decoding and are very efficient. Another variation is that multiple
models are merged into one model removing the need the multi-level inference,
but again, this is a design decision involving compromise on re-usability,
performance and effort. We aim to provide support for all these use cases,
and to allow the analytics pipeline designer to make the best design decisions based
on his specific context.

#### Analytics Meta
The Analytics Meta (GstAnalyticsRelationMeta) is the foundation of re-usability of
analytics results and its goal is to store analytics results (GstAnalyticsMtd)
in an efficient way, and to allow to define relations between them. GstAnalyticsMtd
is very primitive and is meant to be expanded. GstAnalyticsMtdClassification (storage
for classification result), GstAnalyticsMtdObjectDetection (storage for
object detection result), GstAnalyticsMtdTracking (storage for
object tracking) are specialization and can used as reference to create other
storage, based on GstAnalyticsMtd, for other types of analytics result.

There are two major use case for the ability to define relation between
analytics results. The first one is define a relation between analytics results
that were generated at different stages. A good example of this could be a first
analysis detected cars from an image and a second level analysis where only
section of image presenting a car is pushed to a second analysis to extract
brand/model of the car in a section of the image. This analytics result is then
appended to the original image with a relation defined with the object-detection
result that have localized this car in the image.

The other use case for relations is to create composition by re-using existing
GstAnalyticsMtd specialization. The relation between different analytics result is
completely decoupled from the analytics result themselves.

All relation definitions are stored in
GstAnaltyicsRelationMeta, which is a container of GstAnaltyicsMtd and also contains
an adjacency-matrix storing relations. One of the benefits is the ability of a
consumer of analytics meta to explore the graph and follow relations between
analytics results without having to understand every type of result in the
relation path. Another important aspect is that analytics meta are not
specific to machine learning techniques and can also be used to store analysis
results from computer vision, heuristics or other techniques. It can be used as
a bridge between different techniques.

##### Storing Tensors Into Analytics Meta
To be able to describe more precisely analytics results, an analytics pipeline
where the output tensor of the first inference stage is directly pushed, without tensor decoding,
into a second inference stage. It would be useful to store those tensors using
analytics-meta because we could communicate the relation between tensor of first
inference and tensor of second inference. With the relation description a
tensor-decoder of second inference would be able to retrieve associated tensor of
of first inference and extract potentially useful information that is not
available the tensor of the second inference.

### Semantically-Agnostic Tensor Processing
Not all tensor processing is model dependent. Sometime the processing can be
done uniformly on all tensor's values. For example normalization, range
adjustment, offset adjustment, quantization are examples of operations that do
not require knowledge of how the information is encoded in the tensor. To the
contrary of tensor decoder, elements implementing these types of processing
don't need to know how information is encoded in the tensor but need to know
general information about the tensor like: cardinality, dimension and data type.
Note GStreamer already does a lot of semantically-agnostic tensor processing,
remember image/frame are also a form of tensor, processing like scaling,
cropping, color-space conversion, ...

#### Semantically-Agnostic Tensor Processing With Graph-Computing Framework
Graph computing frameworks, like ONNX, can also be used this type of operation.

#### Tensor Decoder Bin And Auto-Plugging
Since tensor decoders are model specific, we expect that many will be created
and one way to simplify analytics pipeline creation an promote re-usability is to provide
tensor decoder bin able to identify the correct tensor decoder for the tensor to
be decoded. It's possible to have multiple tensor decoder able to decode the
exact same decoder, but making use of specific acceleration. Rank can be used to
identify the ideal tensor decoder for a specific platform.

### Tensor Transport Mode
Two transport mode are envisioned as Meta or as Media. Both mode have pros and
cons which justify supporting both mode.

#### Tensor Transport As Meta
In this mode tensor is attached to the buffer (the media) on which the analysis
was performed. The advantage of this mode if the original media is kept in a
direct association with analytics results. Further refinement analysis or
consumption (like overlay) of the analytics result are easier when the media on
which the analysis was performed is available and easily identifiable. Another
advantage is the ability to keep a relation description between tensors in a
refinement context On the other hand this mode of transporting analytics result
make negotiation of tensor-decoder in particular difficult.

#### Tensor Transport As Media
In this mode tensor is a media, potentially referring to buffer (original media on
which the analysis was performed) using a Meta (idea behind OriginalBufferMeta MR).
The advantage of this mode is tensor-decoder negotiation is simple, but
association of the analytics result with the original buffer on which the
analysis was performed is more difficult.
*Also note this the mode of transport used by NNStreamer.*

### Negotiation
Allowing to negotiate the required analysis pre/post-processing and automatically
injecting the required elements to able to perform them would be very valuable and
minimize effort of porting an analytics pipeline between different platforms and
making use of acceleration available. Tensor-decoders bin, auto-plugging of
pre-processing (considering acceleration available), auto-plugging of inference
element (optimized of the platform), post-processing, tensor-decoder bin
selecting required tensor-decoders potentially from multiple functionally
equivalent, but more adapted to the platform are all aspect to consider when
designing negotiation involved in analytics-pipeline.

#### Negotiating Tensor-Decoder
As described above tensor-decoder need to know 4 attributes about a tensor to
know if it can handle it:

1. Tensor dimension cardinality ( not required explicitly in some cases)
2. Tensor dimension
3. Tensor datatype
4. Tensor type (identifier of analytics-result encoding semantic)

Note 1, 2, 3 could be encoded into 4, but this is not desirable because 1,2,3
are useful for selection semantically-agnostic tensor processor.

Tensor-decoder can handle multiple tensor types. This could be expressed in the
sinkpad(s) template by a list of arrays where each combination of tensor types
it can handle would be expressed. This would make the sinkpad(s) caps difficult
to read. To avoid this problem when a tensor-decoder handle multiple tensors the
tensor type is a category the encapsulate all tensor type it can handle.
Referring again to YOLOv3's 3 tensors: small, medium large, all 3 would have the
same tensor-type identifier, ex YOLOv3, and each tensors themselves would have
sub-type field distinguishing them ('small', 'medium', 'large'). Same also
applies to FastSAM 2 tensors ('FastSAM-masks', 'FastSAM-logits') where both
would be represented by the same tensor type ('FastSAM') in pad capability level.

When tensor is stored as a meta, allocation query need to be used to negotiate
tensor-decoder. TODO: expand how this would work.


##### Tensor-Decoder Sinkpad Caps Examples

Examples assuming object-detection on video frame
```
PadTemplates:
  SINK template: 'vsink' // Tensor attached on to buffer
    Avaiability: 'always'
    Capabilities:
      video/x-raw
        format: {...}
        width: {...}
        height: {...}
        framerate: {...}

  SINK template: 'tsink' // Tensor
    Avaiability: 'always'
    Capabilities:
      tensor/x-raw
        shape:{<a, b, ...z>} // This represent a x b x ... x z
        datatype: {(enum) "int8", "float32", ...}
        type: { (string)"YOLOv3", (string)"YOLOv4", (string)"SSD", ...)}

```

##### Tensor-Decoder Srcpad(s)
Typically will be the same as sinkpad but could be different. In general
tensor-decoder only attach an analytics-meta to buffer. Analytics-meta
consumption is left to other downstream elements. It's also possible for
tensor-decoder to have very different caps on srcpad. This can be the case when
analytics-result is difficult to represent like text-to-speech or
super-resolution. In these case the tensor-decoder could be producing a media
directly. audio for TTS or image for super-resolution.

### Inference Sinkpad(s) Capabilities
Sinkpad capability, before been constrained based on model, can be any
media type, including `tensor` . Note that multiple sinkpads can be present.

#### Batch Inference
To support batch inference `tensor` media type need to be used. Batching is
a method used to spread the fixed time cost of scheduling work on some
accelerator (GPU). Multiple samples (buffers) are aggregated into a batch that
is pushed to the inference. When batching is used, output tensor will also contain
analytics results in the form of a batch. Un-batching require information on how
the batch was formed: buffer timestamp, buffer source, media type, buffer caps.
A batch can be formed from a single source, forming the batch with time multiplexing
of from multiple sources, time and source multiplexing. Once multiplexed, the
batch can be pushed to the inference element as a tensor media.

TODO: Describe TensorBatchMeta

### Inference Srcpad(s) Capabilities

Srcpads capabilities, will be identical to sinkpads capabilities or a
```tensor```.

```
PadTemplates:
  SRC template: 'vsrc_%u' // Tensor attached on to buffer
    Avaiability: 'always'
    Capabilities:
      video/x-raw
        format: {...}
        width: {...}
        height: {...}
        framerate: {...}
  SRC template: 'asrc_%u' // Tensor attached on to buffer
    Avaiability: 'always'
    Capabilities:
      audio/x-raw
        format: {...}
        layout: {...}
        rate: [...]
        channels: [...]

  SRC template: 'tsrc_%u' // Tensor attached on to buffer
    Avaiability: 'always'
    Capabilities:
      text/x-raw
        format: {...}

  SRC template: 'src_%u' // Tensor
    Avaiability: 'always'
    Capabilities:
      tensor/x-raw
        shape:{<a, b, ...z>} // This represent a x b x ... x z
        datatype: {(enum) "int8", "float32", ...}
        type: { (string)"YOLOv3", (string)"YOLOv4", (string)"SSD", ...)}

```

### New Video Format
TODO

- We need to add floating point video formats

### Batch Aggregator Element
TODO


# Reference
- [Onnx-Refactor-MR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/4916)
- [Analytics-Meta MR](https://gitlab.freedesktop.org/gstreamer/gstreamer/-/merge_requests/4962)


