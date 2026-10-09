#pragma once

class Epub;

namespace fiction_epub_rich {

class Renderer;

enum class BlockType {
  TEXT_BLOCK,
  IMAGE_BLOCK
};

class Block {
public:
  virtual ~Block() = default;
  virtual void layout(Renderer *renderer, ::Epub *epub, int max_width = -1) = 0;
  virtual void dump() = 0;
  virtual BlockType getType() = 0;
  virtual bool isEmpty() = 0;
  virtual void finish() {}
};

}  // namespace fiction_epub_rich
