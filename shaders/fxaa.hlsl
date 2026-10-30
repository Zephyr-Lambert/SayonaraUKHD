// UmiGraphics FXAA implementation; 0BSD. No vendor shader source embedded.
// Edge orientation, bounded endpoint search and subpixel contrast smoothing.
Texture2D scene : register(t0);
SamplerState linearClamp : register(s0);
cbuffer Settings : register(b0) {
    float2 pixelSize;
    float2 maxUV;
};
float4 VSMain(uint vertex : SV_VertexID) : SV_POSITION {
    float2 corner = float2(vertex == 1 ? 3.0 : -1.0, vertex == 2 ? -3.0 : 1.0);
    return float4(corner, 0.0, 1.0);
}
float4 sampleScene(float2 uv) {
    return scene.SampleLevel(linearClamp, clamp(uv, pixelSize * 0.5, maxUV), 0);
}
float luminance(float3 rgb) { return dot(rgb, float3(0.299, 0.587, 0.114)); }
float readLuma(float2 uv) { return luminance(sampleScene(uv).rgb); }
float4 PSMain(float4 position : SV_POSITION) : SV_TARGET {
    float2 uv = position.xy * pixelSize;
    float4 center = sampleScene(uv);
    float m = luminance(center.rgb);
    float n = readLuma(uv - float2(0, pixelSize.y));
    float s = readLuma(uv + float2(0, pixelSize.y));
    float w = readLuma(uv - float2(pixelSize.x, 0));
    float e = readLuma(uv + float2(pixelSize.x, 0));
    float low = min(m, min(min(n, s), min(w, e)));
    float high = max(m, max(max(n, s), max(w, e)));
    float contrast = high - low;
    if (contrast < max(0.03125, high * 0.125)) return center;

    float nw = readLuma(uv - pixelSize);
    float ne = readLuma(uv + float2(pixelSize.x, -pixelSize.y));
    float sw = readLuma(uv + float2(-pixelSize.x, pixelSize.y));
    float se = readLuma(uv + pixelSize);
    float horizontal = abs(nw + sw - 2*w) + 2*abs(n + s - 2*m) + abs(ne + se - 2*e);
    float vertical = abs(nw + ne - 2*n) + 2*abs(w + e - 2*m) + abs(sw + se - 2*s);
    bool alongX = horizontal >= vertical;
    float first = alongX ? n : w;
    float second = alongX ? s : e;
    bool towardFirst = abs(first-m) >= abs(second-m);
    float neighbor = towardFirst ? first : second;
    float gradient = abs(neighbor-m);
    float average = (neighbor+m)*0.5;
    float2 perpendicular = alongX ? float2(0,pixelSize.y) : float2(pixelSize.x,0);
    if(towardFirst) perpendicular = -perpendicular;
    float2 tangent = alongX ? float2(pixelSize.x,0) : float2(0,pixelSize.y);
    float2 edge = uv + perpendicular*0.5;
    float leftDifference = 0, rightDifference = 0;
    float leftDistance = 12, rightDistance = 12;
    bool foundLeft = false, foundRight = false;
    [unroll] for(int step=1; step<=12; ++step) {
        if(!foundLeft) {
            leftDifference = readLuma(edge-tangent*step)-average;
            if(abs(leftDifference)>=gradient*0.25) {foundLeft=true;leftDistance=step;}
        }
        if(!foundRight) {
            rightDifference = readLuma(edge+tangent*step)-average;
            if(abs(rightDifference)>=gradient*0.25) {foundRight=true;rightDistance=step;}
        }
    }
    bool closerLeft = leftDistance < rightDistance;
    float endDifference = closerLeft ? leftDifference : rightDifference;
    float edgeShift = 0;
    if ((closerLeft ? foundLeft : foundRight) && ((endDifference<0)!=(m-average<0)))
        edgeShift = 0.5-min(leftDistance,rightDistance)/(leftDistance+rightDistance);
    float neighborhood = (2*(n+s+w+e)+nw+ne+sw+se)/12;
    float subpixel = saturate(abs(neighborhood-m)/contrast);
    subpixel = subpixel*subpixel*(3-2*subpixel);
    subpixel = subpixel*subpixel*0.75;
    float3 rgb = sampleScene(uv + perpendicular*max(edgeShift,subpixel)).rgb;
    return float4(rgb,center.a);
}
