<?xml version="1.0" encoding="utf-8"?>
<!DOCTYPE TS>
<TS version="2.1" language="zh_CN" sourcelanguage="en">
<context>
    <name>Bayer16Decoder</name>
    <message>
        <location filename="bayerdecoders.cpp" line="109"/>
        <source>%1=%2</source>
        <translation>%1=%2</translation>
    </message>
</context>
<context>
    <name>PackedRgb16Decoder</name>
    <message>
        <location filename="rgbdecoders.cpp" line="437"/>
        <source>%1 stride must be at least the width times %2 bytes. Received width %3, stride %4.</source>
        <translation type="unfinished">%1 的 stride 至少为宽度乘以 %2 字节。当前宽度 %3，stride %4。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="445"/>
        <source>%1 scanline must be at least the height. Received height %2, scanline %3.</source>
        <translation type="unfinished">%1 的 scanline 至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
</context>
<context>
    <name>PackedRgbDecoder</name>
    <message>
        <location filename="rgbdecoders.cpp" line="39"/>
        <source>%1 stride must be at least the width times %2 bytes. Received width %3, stride %4.</source>
        <translation>%1 的 stride 至少为宽度乘以 %2 字节。当前宽度 %3，stride %4。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="47"/>
        <source>%1 scanline must be at least the height. Received height %2, scanline %3.</source>
        <translation>%1 的 scanline 至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="74"/>
        <source>Could not allocate the converted QImage.</source>
        <translation>无法分配转换后的 QImage。</translation>
    </message>
</context>
<context>
    <name>PackedYuv422Decoder</name>
    <message>
        <location filename="yuvdecoders.cpp" line="558"/>
        <source>%1 width must be even. Received %2.</source>
        <translation>%1 的宽度必须为偶数。当前为 %2。</translation>
    </message>
    <message>
        <location filename="yuvdecoders.cpp" line="563"/>
        <source>%1 stride must be at least twice the width. Received width %2, stride %3.</source>
        <translation>%1 的 stride 至少为宽度的两倍。当前宽度 %2，stride %3。</translation>
    </message>
    <message>
        <location filename="yuvdecoders.cpp" line="570"/>
        <source>%1 scanline must be at least the height. Received height %2, scanline %3.</source>
        <translation>%1 的 scanline 至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
</context>
<context>
    <name>PlanarYuv420p16Decoder</name>
    <message>
        <location filename="yuvdecoders.cpp" line="423"/>
        <source>%1 stride must be at least twice the width (16-bit samples). Received width %2, stride %3.</source>
        <translation>%1 的 stride 至少为宽度的两倍（16 位采样）。当前宽度 %2，stride %3。</translation>
    </message>
</context>
<context>
    <name>RawImageDecoder</name>
    <message>
        <location filename="rawimagedecoder.cpp" line="19"/>
        <source>Width and height must be between %1 and %2.</source>
        <translation>宽度和高度必须介于 %1 和 %2 之间。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="24"/>
        <source>Stride and scanline must both be positive.</source>
        <translation>stride 和 scanline 都必须为正数。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="27"/>
        <source>Stride must not exceed %1 and scanline must not exceed %2.</source>
        <translation>stride 不能超过 %1，scanline 不能超过 %2。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="43"/>
        <source>%1 does not support separate plane viewing.</source>
        <translation>%1 不支持分量平面单独查看。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="60"/>
        <source>Cannot open the file: %1</source>
        <translation>无法打开文件：%1</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="67"/>
        <source>The calculated %1 frame size is invalid.</source>
        <translation>计算得到的 %1 帧大小无效。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="71"/>
        <source>The %1 frame is too large to load into memory.</source>
        <translation>%1 帧过大，无法加载到内存中。</translation>
    </message>
    <message>
        <source>File size does not match the %1 layout. Expected %2 bytes, found %3 bytes (width=%4, height=%5, stride=%6, scanline=%7).</source>
        <translation type="vanished">文件大小与 %1 布局不匹配。预期 %2 字节，实际 %3 字节（宽度=%4，高度=%5，stride=%6，scanline=%7）。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="76"/>
        <source>File size does not match whole %1 frames. Frame size is %2 bytes, file size is %3 bytes (width=%4, height=%5, stride=%6, scanline=%7).</source>
        <translation>文件大小不是 %1 整帧的整数倍。帧大小为 %2 字节，文件大小为 %3 字节（width=%4，height=%5，stride=%6，scanline=%7）。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="90"/>
        <source>Frame %1 is out of range; the file contains %2 frames.</source>
        <translation>第 %1 帧超出范围；文件包含 %2 帧。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="96"/>
        <location filename="rawimagedecoder.cpp" line="117"/>
        <source>Failed while reading the file: %1</source>
        <translation>读取文件失败：%1</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="124"/>
        <source>Loading canceled.</source>
        <translation>加载已取消。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="105"/>
        <source>Not enough memory to allocate %1 bytes for the image frame.</source>
        <translation>内存不足，无法为图像帧分配 %1 字节。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="109"/>
        <source>Could not allocate %1 bytes for the image frame.</source>
        <translation>无法为图像帧分配 %1 字节。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder.cpp" line="128"/>
        <source>The file read was incomplete. Expected %1 bytes, received %2 bytes.</source>
        <translation>文件读取不完整。预期 %1 字节，实际收到 %2 字节。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="36"/>
        <source>%1 width and height must both be even. Received %2x%3.</source>
        <translation>%1 的宽度和高度都必须为偶数。当前为 %2x%3。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="43"/>
        <location filename="rawimagedecoder_p.h" line="76"/>
        <source>%1 stride must be even and at least the width. Received width %2, stride %3.</source>
        <translation>%1 的 stride 必须为偶数且至少为宽度。当前宽度 %2，stride %3。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="50"/>
        <source>%1 scanline must be even and at least the height. Received height %2, scanline %3.</source>
        <translation>%1 的 scanline 必须为偶数且至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="71"/>
        <source>%1 width must be even. Received %2.</source>
        <translation>%1 的宽度必须为偶数。当前为 %2。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="83"/>
        <location filename="rawimagedecoder_p.h" line="122"/>
        <source>%1 scanline must be at least the height. Received height %2, scanline %3.</source>
        <translation>%1 的 scanline 至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
    <message>
        <source>%1 sample depth must be between 8 and 16 bits. Received %2.</source>
        <translation>%1 的样本位深必须介于 8 和 16 位之间。当前为 %2。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="108"/>
        <source>%1 stride must be at least the width. Received width %2, stride %3.</source>
        <translation>%1 的 stride 至少为宽度。当前宽度 %2，stride %3。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="114"/>
        <source>%1 stride must be at least the width times %2 bytes. Received width %3, stride %4.</source>
        <translation>%1 的 stride 至少为宽度乘以 %2 字节。当前宽度 %3，stride %4。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="99"/>
        <source>OpenCV returned an empty image or unexpected dimensions.</source>
        <translation>OpenCV 返回了空图像或意外的尺寸。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="107"/>
        <source>Could not allocate the converted QImage.</source>
        <translation>无法分配转换后的 QImage。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="118"/>
        <source>OpenCV conversion failed: %1</source>
        <translation>OpenCV 转换失败：%1</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="121"/>
        <source>%1 conversion failed: %2</source>
        <translation>%1 转换失败：%2</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="125"/>
        <source>%1 conversion failed with an unknown exception.</source>
        <translation>%1 转换失败：发生未知异常。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="137"/>
        <location filename="rawimagedecoder_p.h" line="148"/>
        <location filename="rawimagedecoder_p.h" line="165"/>
        <location filename="rawimagedecoder_p.h" line="200"/>
        <location filename="rawimagedecoder_p.h" line="216"/>
        <source>Could not allocate the plane image.</source>
        <translation>无法分配平面图像。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="182"/>
        <source>Invalid plane index %1.</source>
        <translation>无效的平面索引 %1。</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="228"/>
        <source>Y=%1 U=%2 V=%3</source>
        <translation>Y=%1 U=%2 V=%3</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="273"/>
        <source>Y=%1 U=%2 V=%3 A=%4</source>
        <translation>Y=%1 U=%2 V=%3 A=%4</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="233"/>
        <source>R=%1 G=%2 B=%3</source>
        <translation>R=%1 G=%2 B=%3</translation>
    </message>
    <message>
        <location filename="rawimagedecoder_p.h" line="238"/>
        <source>R=%1 G=%2 B=%3 A=%4</source>
        <translation>R=%1 G=%2 B=%3 A=%4</translation>
    </message>
</context>
<context>
    <name>RawImageFileName</name>
    <message>
        <location filename="rawimagefilename.cpp" line="34"/>
        <source>Invalid %1 value &quot;%2&quot; in the file name.</source>
        <translation>文件名中的 %1 值 &quot;%2&quot; 无效。</translation>
    </message>
    <message>
        <source>The file name contains a %1 tag that cannot be parsed. Expected &quot;_%1[number]&quot;.</source>
        <translation>文件名中的 %1 标签无法解析。预期为 &quot;_%1[数字]&quot;。</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="81"/>
        <source>Pipeline</source>
        <translation>处理管线</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="83"/>
        <source>Output</source>
        <translation>输出端口</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="105"/>
        <location filename="rawimagefilename.cpp" line="148"/>
        <source>width</source>
        <translation>宽度</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="108"/>
        <location filename="rawimagefilename.cpp" line="151"/>
        <source>height</source>
        <translation>高度</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="115"/>
        <source>stride</source>
        <translation>stride</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="120"/>
        <source>scanline</source>
        <translation>scanline</translation>
    </message>
    <message>
        <location filename="rawimagefilename.cpp" line="132"/>
        <source>The file name contains incomplete image metadata. Expected &quot;_w[width]_h[height]_stride[stride]_scanline[scanline]&quot;.</source>
        <translation>文件名中的图像元数据不完整。预期为 &quot;_w[width]_h[height]_stride[stride]_scanline[scanline]&quot;。</translation>
    </message>
</context>
<context>
    <name>RawImageFrame</name>
    <message>
        <location filename="rawimageframe.cpp" line="31"/>
        <source>No decoder is available for the loaded image.</source>
        <translation>没有可用于已加载图像的解码器。</translation>
    </message>
    <message>
        <location filename="rawimageframe.cpp" line="35"/>
        <source>The selected format produced an invalid frame size.</source>
        <translation>所选格式计算出了无效的帧大小。</translation>
    </message>
    <message>
        <location filename="rawimageframe.cpp" line="38"/>
        <source>Loaded data size (%1 bytes) does not match the %2 frame size (%3 bytes). The format may have changed while the image was loading.</source>
        <translation>已加载数据大小（%1 字节）与 %2 帧大小（%3 字节）不匹配。图像加载期间格式可能发生了变化。</translation>
    </message>
</context>
<context>
    <name>RawImageHistogram</name>
    <message>
        <location filename="rawimagehistogram.cpp" line="130"/>
        <source>%1  (mean %2)</source>
        <translation>%1（均值 %2）</translation>
    </message>
    <message>
        <location filename="rawimagehistogram.cpp" line="153"/>
        <source>Value</source>
        <translation>像素值</translation>
    </message>
    <message>
        <location filename="rawimagehistogram.cpp" line="171"/>
        <source>Count</source>
        <translation>像素数</translation>
    </message>
</context>
<context>
    <name>Rgba64Decoder</name>
    <message>
        <location filename="rgbdecoders.cpp" line="542"/>
        <source>Could not allocate the converted QImage.</source>
        <translation type="unfinished">无法分配转换后的 QImage。</translation>
    </message>
</context>
<context>
    <name>SemiPlanarYuv420p16Decoder</name>
    <message>
        <location filename="yuvdecoders.cpp" line="145"/>
        <source>%1 stride must be at least twice the width (16-bit samples). Received width %2, stride %3.</source>
        <translation>%1 的 stride 至少为宽度的两倍（16 位采样）。当前宽度 %2，stride %3。</translation>
    </message>
</context>
<context>
    <name>Y8Decoder</name>
    <message>
        <location filename="rgbdecoders.cpp" line="569"/>
        <source>%1 stride must be at least the width. Received width %2, stride %3.</source>
        <translation>%1 的 stride 至少为宽度。当前宽度 %2，stride %3。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="576"/>
        <source>%1 scanline must be at least the height. Received height %2, scanline %3.</source>
        <translation>%1 的 scanline 至少为高度。当前高度 %2，scanline %3。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="600"/>
        <source>Could not allocate the converted QImage.</source>
        <translation>无法分配转换后的 QImage。</translation>
    </message>
    <message>
        <location filename="rgbdecoders.cpp" line="619"/>
        <source>Y=%1</source>
        <translation>Y=%1</translation>
    </message>
</context>
<context>
    <name>YuvControls</name>
    <message>
        <location filename="yuvcontrols.cpp" line="146"/>
        <location filename="yuvcontrols.cpp" line="238"/>
        <source>Composite</source>
        <translation>合成图像</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="220"/>
        <source>Matches the file size: %1 bytes per frame, %2 frames.</source>
        <translation>匹配文件大小：每帧 %1 字节，共 %2 帧。</translation>
    </message>
    <message>
        <source>%1 bit</source>
        <translation>%1 位</translation>
    </message>
    <message>
        <source>%1 bit MSB</source>
        <translation>%1 位 左对齐</translation>
    </message>
    <message>
        <source>%1 bit LSB</source>
        <translation>%1 位 右对齐</translation>
    </message>
    <message>
        <source>Where the significant bits sit inside each 16-bit sample. MSB means the
value is left-aligned and the low bits are padding (P010 and most ISP
output); LSB means it is right-aligned and the high bits are zero (most
sensor dumps). Reading right-aligned data as 16 bit yields a black frame.</source>
        <translation>有效位在每个 16 位样本中的位置。左对齐表示数值靠高位、低位是填充
（P010 以及大多数 ISP 输出）；右对齐表示数值靠低位、高位为零（大多数
传感器 dump）。把右对齐的数据按 16 位读会得到一张纯黑的图。</translation>
    </message>
    <message>
        <source>Samples:</source>
        <translation>样本：</translation>
    </message>
    <message>
        <source>View:</source>
        <translation>显示：</translation>
    </message>
    <message>
        <source>Linear</source>
        <translation>线性</translation>
    </message>
    <message>
        <source>Gamma 2.2</source>
        <translation>Gamma 2.2</translation>
    </message>
    <message>
        <source>Auto level</source>
        <translation>自动电平</translation>
    </message>
    <message>
        <source>Auto + Gamma 2.2</source>
        <translation>自动电平 + Gamma 2.2</translation>
    </message>
    <message>
        <source>Auto + WB + Gamma 2.2</source>
        <translation>自动电平 + 白平衡 + Gamma 2.2</translation>
    </message>
    <message>
        <source>Display-only. Does not change the samples the probe and histogram read.
Linear shows the decoded values as they are. Auto stretches the range.
WB equalizes the channel means (gray-world).</source>
        <translation>仅影响显示，不改变探针和直方图读到的样本。
线性按解码值原样显示。自动电平拉伸动态范围。
白平衡按灰世界假设拉齐各通道均值。</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="230"/>
        <source>Width:</source>
        <translation>宽度：</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="231"/>
        <source>Height:</source>
        <translation>高度：</translation>
    </message>
    <message>
        <source>Stride:</source>
        <translation>Stride:</translation>
    </message>
    <message>
        <source>Scanline:</source>
        <translation>Scanline:</translation>
    </message>
    <message>
        <source> bytes</source>
        <translation> 字节</translation>
    </message>
    <message>
        <source> lines</source>
        <translation> 行</translation>
    </message>
    <message>
        <source>Filled from the current format: stride is the tightly packed row in bytes,
scanline is the height. Edit them when the file has row or plane padding.</source>
        <translation>按当前格式自动填写：stride 是紧凑行的字节数，scanline 等于高度。
文件有行填充或面填充时再改这两个值。</translation>
    </message>
    <message>
        <source>This file is larger than a tightly packed frame, or the name declares padding.
Stride is the first plane's row size in bytes; scanline is its row count.</source>
        <translation>文件比紧凑帧大，或文件名声明了 padding。
Stride 是首平面每行字节数，scanline 是首平面行数。</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="232"/>
        <source>Format:</source>
        <translation>格式：</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="233"/>
        <source>Frame:</source>
        <translation>帧:</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="234"/>
        <source>Plane:</source>
        <translation>平面:</translation>
    </message>
    <message>
        <location filename="yuvcontrols.cpp" line="235"/>
        <location filename="yuvcontrols.cpp" line="236"/>
        <source> px</source>
        <translation> 像素</translation>
    </message>
</context>
<context>
    <name>YuvViewer</name>
    <message>
        <source>Samples</source>
        <translation>样本</translation>
    </message>
    <message>
        <source>%1 bit</source>
        <translation>%1 位</translation>
    </message>
    <message>
        <source>%1 bit, left-aligned in 16</source>
        <translation>%1 位，在 16 位中左对齐</translation>
    </message>
    <message>
        <source>%1 bit, right-aligned in 16</source>
        <translation>%1 位，在 16 位中右对齐</translation>
    </message>
    <message>
        <source>Invalid %1 value &quot;%2&quot; in the file name.</source>
        <translation type="vanished">文件名中的 %1 值 &quot;%2&quot; 无效。</translation>
    </message>
    <message>
        <source>width</source>
        <translation type="vanished">宽度</translation>
    </message>
    <message>
        <source>height</source>
        <translation type="vanished">高度</translation>
    </message>
    <message>
        <source>stride</source>
        <translation type="vanished">stride</translation>
    </message>
    <message>
        <source>scanline</source>
        <translation type="vanished">scanline</translation>
    </message>
    <message>
        <source>The file name contains incomplete image metadata. Expected &quot;_w[width]_h[height]_stride[stride]_scanline[scanline]&quot;.</source>
        <translation type="vanished">文件名中的图像元数据不完整。预期为 &quot;_w[width]_h[height]_stride[stride]_scanline[scanline]&quot;。</translation>
    </message>
    <message>
        <source>No decoder is available for the loaded image.</source>
        <translation type="vanished">没有可用于已加载图像的解码器。</translation>
    </message>
    <message>
        <source>The selected format produced an invalid frame size.</source>
        <translation type="vanished">所选格式计算出了无效的帧大小。</translation>
    </message>
    <message>
        <source>Loaded data size (%1 bytes) does not match the %2 frame size (%3 bytes). The format may have changed while the image was loading.</source>
        <translation type="vanished">已加载数据大小（%1 字节）与 %2 帧大小（%3 字节）不匹配。图像加载期间格式可能发生了变化。</translation>
    </message>
    <message>
        <source>%1  (mean %2)</source>
        <translation type="vanished">%1（均值 %2）</translation>
    </message>
    <message>
        <source>Value</source>
        <translation type="vanished">像素值</translation>
    </message>
    <message>
        <source>Count</source>
        <translation type="vanished">像素数</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="205"/>
        <location filename="yuvviewer.cpp" line="799"/>
        <source>Info</source>
        <translation>信息</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="210"/>
        <location filename="yuvviewer.cpp" line="804"/>
        <source>Histogram</source>
        <translation>直方图</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="217"/>
        <source>Enter the image width and height, then select Reload.</source>
        <translation>请输入图像宽度和高度，然后选择“重新加载”。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="225"/>
        <location filename="yuvviewer.cpp" line="432"/>
        <location filename="yuvviewer.cpp" line="683"/>
        <source>open</source>
        <translation>打开</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="355"/>
        <source>The YUV viewer is not fully initialized.</source>
        <translation>YUV 查看器尚未完全初始化。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="421"/>
        <source>Not enough memory to load and render the image.</source>
        <translation>内存不足，无法加载和渲染图像。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="424"/>
        <source>Unexpected error while loading the image: %1</source>
        <translation>加载图像时发生意外错误：%1</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="428"/>
        <source>An unknown error occurred while loading the image.</source>
        <translation>加载图像时发生未知错误。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="513"/>
        <source>The selected format no longer matches the loaded image. Reload the file.</source>
        <translation>所选格式与已加载图像不再匹配。请重新加载文件。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="396"/>
        <source>Loading...</source>
        <translation>正在加载...</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="432"/>
        <source>Loading... %1%</source>
        <translation>正在加载... %1%</translation>
    </message>
    <message>
        <source>Opened &quot;%1&quot;, %2x%3, %4 (stride=%5, scanline=%6).</source>
        <translation type="vanished">已打开 &quot;%1&quot;，%2x%3，%4（stride=%5，scanline=%6）。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="449"/>
        <source>&quot;%1&quot;, %2x%3, %4 (stride=%5, scanline=%6)</source>
        <translation>“%1”，%2x%3，%4（stride=%5，scanline=%6）</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="457"/>
        <source>Opened %1, frame %2/%3.</source>
        <translation>已打开 %1，第 %2/%3 帧。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="462"/>
        <source>Opened %1.</source>
        <translation>已打开 %1。</translation>
    </message>
    <message>
        <source>Composite</source>
        <translation type="vanished">合成图像</translation>
    </message>
    <message>
        <source>Matches the file size: %1 bytes per frame, %2 frames.</source>
        <translation type="vanished">匹配文件大小：每帧 %1 字节，共 %2 帧。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="598"/>
        <source>File</source>
        <translation>文件</translation>
    </message>
    <message>
        <source>Pipeline</source>
        <translation type="vanished">处理管线</translation>
    </message>
    <message>
        <source>Output</source>
        <translation type="vanished">输出端口</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="605"/>
        <source>Format</source>
        <translation>格式</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="606"/>
        <source>Width</source>
        <translation>宽度</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="606"/>
        <location filename="yuvviewer.cpp" line="607"/>
        <source>%1 px</source>
        <translation>%1 像素</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="607"/>
        <source>Height</source>
        <translation>高度</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="608"/>
        <source>Stride</source>
        <translation>Stride</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="608"/>
        <source>%1 bytes</source>
        <translation>%1 字节</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="609"/>
        <source>Scanline</source>
        <translation>Scanline</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="609"/>
        <source>%1 lines</source>
        <translation>%1 行</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="610"/>
        <source>Y plane size</source>
        <translation>Y 平面大小</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="612"/>
        <source>Frame size</source>
        <translation>帧大小</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="613"/>
        <source>File size</source>
        <translation>文件大小</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="614"/>
        <source>Frames</source>
        <translation>帧数</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="649"/>
        <source>(%1, %2)  %3</source>
        <translation>(%1, %2)  %3</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="653"/>
        <source>probe</source>
        <translation>探针</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="678"/>
        <source>
File: %1</source>
        <translation>
文件：%1</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="756"/>
        <source>PNG image (*.png);;BMP image (*.bmp)</source>
        <translation>PNG 图像 (*.png);;BMP 图像 (*.bmp)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="758"/>
        <source>Export Image</source>
        <translation>导出图像</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="770"/>
        <source>Failed to save the image to &quot;%1&quot;.</source>
        <translation>无法将图像保存到“%1”。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="774"/>
        <source>Exported &quot;%1&quot;.</source>
        <translation>已导出“%1”。</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="774"/>
        <source>export</source>
        <translation>导出</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="791"/>
        <source>YUV Image</source>
        <translation>YUV 图像</translation>
    </message>
    <message>
        <source>Width:</source>
        <translation type="vanished">宽度：</translation>
    </message>
    <message>
        <source>Height:</source>
        <translation type="vanished">高度：</translation>
    </message>
    <message>
        <source>Format:</source>
        <translation type="vanished">格式：</translation>
    </message>
    <message>
        <source>Frame:</source>
        <translation type="vanished">帧:</translation>
    </message>
    <message>
        <source>Plane:</source>
        <translation type="vanished">平面:</translation>
    </message>
    <message>
        <source> px</source>
        <translation type="vanished"> 像素</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="779"/>
        <source>&amp;Reload</source>
        <translation>重新加载(&amp;R)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="780"/>
        <source>Previous Frame</source>
        <translation>上一帧</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="781"/>
        <source>Next Frame</source>
        <translation>下一帧</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="782"/>
        <source>Zoom &amp;In</source>
        <translation>放大(&amp;I)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="783"/>
        <source>Zoom &amp;Out</source>
        <translation>缩小(&amp;O)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="784"/>
        <source>Reset Zoom</source>
        <translation>重置缩放</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="785"/>
        <source>&amp;Fit to Window</source>
        <translation>适应窗口(&amp;F)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="786"/>
        <source>&amp;Smooth Scaling</source>
        <translation>平滑缩放(&amp;S)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="787"/>
        <source>Pixel &amp;Grid</source>
        <translation>像素网格(&amp;G)</translation>
    </message>
    <message>
        <location filename="yuvviewer.cpp" line="788"/>
        <source>&amp;Export...</source>
        <translation>导出(&amp;E)...</translation>
    </message>
</context>
</TS>
