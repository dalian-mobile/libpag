/////////////////////////////////////////////////////////////////////////////////////////////////
//
//  Tencent is pleased to support the open source community by making libpag available.
//
//  Copyright (C) 2021 THL A29 Limited, a Tencent company. All rights reserved.
//
//  Licensed under the Apache License, Version 2.0 (the "License"); you may not use this file
//  except in compliance with the License. You may obtain a copy of the License at
//
//      http://www.apache.org/licenses/LICENSE-2.0
//
//  unless required by applicable law or agreed to in writing, software distributed under the
//  license is distributed on an "as is" basis, without warranties or conditions of any kind,
//  either express or implied. see the license for the specific language governing permissions
//  and limitations under the license.
//
/////////////////////////////////////////////////////////////////////////////////////////////////

#include "GLYUVTextureEffect.h"
#include "gpu/YUVTextureEffect.h"

namespace tgfx {
void GLYUVTextureEffect::emitCode(EmitArgs& args) {
  const auto* yuvFP = static_cast<const YUVTextureEffect*>(args.fragmentProcessor);
  auto* fragBuilder = args.fragBuilder;
  auto* uniformHandler = args.uniformHandler;

  auto yuvTexture = yuvFP->texture;
  fragBuilder->codeAppend("vec3 yuv;");
  fragBuilder->codeAppend("yuv.x = ");
  fragBuilder->appendTextureLookup((*args.textureSamplers)[0], (*args.transformedCoords)[0].name());
  fragBuilder->codeAppend(".r;");
  if (yuvTexture->pixelFormat() == YUVPixelFormat::I420) {
    fragBuilder->codeAppend("yuv.y = ");
    fragBuilder->appendTextureLookup((*args.textureSamplers)[1],
                                     (*args.transformedCoords)[0].name());
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend("yuv.z = ");
    fragBuilder->appendTextureLookup((*args.textureSamplers)[2],
                                     (*args.transformedCoords)[0].name());
    fragBuilder->codeAppend(".r;");
  } else if (yuvTexture->pixelFormat() == YUVPixelFormat::NV12) {
    fragBuilder->codeAppend("yuv.yz = ");
    fragBuilder->appendTextureLookup((*args.textureSamplers)[1],
                                     (*args.transformedCoords)[0].name());
    fragBuilder->codeAppend(".ra;");
  }
  if (IsLimitedYUVColorRange(yuvFP->texture->colorSpace())) {
    fragBuilder->codeAppend("yuv.x -= (16.0 / 255.0);");
  }
  fragBuilder->codeAppend("yuv.yz -= vec2(0.5, 0.5);");
  std::string mat3Name;
  mat3ColorConversionUniform = uniformHandler->addUniform(
      ShaderFlags::Fragment, ShaderVar::Type::Float3x3, "Mat3ColorConversion", &mat3Name);
  fragBuilder->codeAppendf("vec3 rgb = clamp(%s * yuv, 0.0, 1.0);", mat3Name.c_str());
  if (yuvFP->alphaStart == Point::Zero()) {
    fragBuilder->codeAppendf("%s = vec4(rgb, 1.0) * %s;", args.outputColor.c_str(),
                             args.inputColor.c_str());
  } else {
    std::string alphaStartName;
    alphaStartUniform = uniformHandler->addUniform(ShaderFlags::Fragment, ShaderVar::Type::Float2,
                                                   "AlphaStart", &alphaStartName);
    std::string alphaVertexColor = "alphaVertexColor";
    fragBuilder->codeAppendf("vec2 %s = %s + %s;", alphaVertexColor.c_str(),
                             (*args.transformedCoords)[0].name().c_str(), alphaStartName.c_str());
    fragBuilder->codeAppend("float yuv_a = ");
    fragBuilder->appendTextureLookup((*args.textureSamplers)[0], alphaVertexColor);
    fragBuilder->codeAppend(".r;");
    fragBuilder->codeAppend(
        "// 为避免因压缩误差、精度等原因造成不透明变成部分透明（比如255变成254），\n");
    fragBuilder->codeAppend("// 下面进行了减1.0/255.0的精度修正。\n");
    fragBuilder->codeAppend("yuv_a = (yuv_a - 16.0/255.0) / (219.0/255.0 - 1.0/255.0);");
    fragBuilder->codeAppend("yuv_a = clamp(yuv_a, 0.0, 1.0);");
    fragBuilder->codeAppendf("%s = vec4(rgb * yuv_a, yuv_a) * %s;", args.outputColor.c_str(),
                             args.inputColor.c_str());
  }
}

static const float ColorConversion601LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.391762f, 2.017232f, 1.596027f, -0.812968f, 0.0f,
};

static const float ColorConversion601FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772f, 1.402f, -0.714136f, 0.0f,
};

static const float ColorConversion709LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.213249f, 2.112402f, 1.792741f, -0.532909f, 0.0f,
};

static const float ColorConversion709FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.187324f, 1.8556f, 1.5748f, -0.468124f, 0.0f,
};

static const float ColorConversion2020LimitRange[] = {
    1.164384f, 1.164384f, 1.164384f, 0.0f, -0.187326f, 2.141772f, 1.678674f, -0.650424f, 0.0f,
};

static const float ColorConversion2020FullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.164553f, 1.8814f, 1.4746f, -0.571353f, 0.0f,
};

static const float ColorConversionJPEGFullRange[] = {
    1.0f, 1.0f, 1.0f, 0.0f, -0.344136f, 1.772000f, 1.402f, -0.714136f, 0.0f,
};

void GLYUVTextureEffect::onSetData(const ProgramDataManager& programDataManager,
                                   const FragmentProcessor& fragmentProcessor) {
  const auto& yuvFP = static_cast<const YUVTextureEffect&>(fragmentProcessor);
  if (alphaStartUniform.isValid()) {
    auto alphaStart = yuvFP.texture->getTextureCoord(yuvFP.alphaStart.x, yuvFP.alphaStart.y);
    if (alphaStart != alphaStartPrev) {
      alphaStartPrev = alphaStart;
      programDataManager.set2f(alphaStartUniform, alphaStart.x, alphaStart.y);
    }
  }
  if (yuvFP.texture->colorSpace() != colorSpacePrev) {
    colorSpacePrev = yuvFP.texture->colorSpace();
    switch (yuvFP.texture->colorSpace()) {
      case YUVColorSpace::BT601_LIMITED:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion601LimitRange);
        break;
      case YUVColorSpace::BT601_FULL:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion601FullRange);
        break;
      case YUVColorSpace::BT709_LIMITED:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion709LimitRange);
        break;
      case YUVColorSpace::BT709_FULL:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion709FullRange);
        break;
      case YUVColorSpace::BT2020_LIMITED:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion2020LimitRange);
        break;
      case YUVColorSpace::BT2020_FULL:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversion2020FullRange);
        break;
      case YUVColorSpace::JPEG_FULL:
        programDataManager.setMatrix3f(mat3ColorConversionUniform, ColorConversionJPEGFullRange);
        break;
      default:
        break;
    }
  }
}
}  // namespace tgfx