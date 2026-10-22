#ifndef UNIT_TEST
#include <esp_log.h>
#else
#define ESP_LOGI(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGE(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGD(tag, args...) \
  printf(args);                \
  printf("\n");
#define ESP_LOGW(tag, args...) \
  printf(args);                \
  printf("\n");
#endif

#include <algorithm>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <vector>

#include "tinyxml2/tinyxml2.h"

#include "Epub.h"
#include "ZipFile.h"

static const char *TAG = "EPUB";

static bool element_name_matches(const char *name, const char *wanted)
{
  if (!name || !wanted)
  {
    return false;
  }
  if (strcmp(name, wanted) == 0)
  {
    return true;
  }

  const size_t name_len = strlen(name);
  const size_t wanted_len = strlen(wanted);
  if (name_len <= wanted_len + 1)
  {
    return false;
  }

  if (name[name_len - wanted_len - 1] != ':')
  {
    return false;
  }

  return strcmp(name + name_len - wanted_len, wanted) == 0;
}

static tinyxml2::XMLElement *find_child_element_by_name(tinyxml2::XMLElement *parent, const char *wanted)
{
  if (!parent)
  {
    return nullptr;
  }

  for (auto *child = parent->FirstChildElement(); child; child = child->NextSiblingElement())
  {
    if (element_name_matches(child->Name(), wanted))
    {
      return child;
    }
  }
  return nullptr;
}

static const char *find_child_text_by_name(tinyxml2::XMLElement *parent, const char *wanted)
{
  auto *child = find_child_element_by_name(parent, wanted);
  return child ? child->GetText() : nullptr;
}

bool Epub::find_content_opf_file(ZipFile &zip, std::string &content_opf_file)
{
  char *meta_info = (char *)zip.read_file_to_memory("META-INF/container.xml");
  if (!meta_info)
  {
    ESP_LOGE(TAG, "Could not find META-INF/container.xml");
    return false;
  }

  tinyxml2::XMLDocument meta_data_doc;
  auto result = meta_data_doc.Parse(meta_info);
  free(meta_info);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Could not parse META-INF/container.xml");
    return false;
  }

  auto *container = find_child_element_by_name(meta_data_doc.RootElement(), "container");
  if (!container)
  {
    container = meta_data_doc.RootElement();
  }
  if (!container)
  {
    ESP_LOGE(TAG, "Could not find container root element");
    return false;
  }

  auto *rootfiles = find_child_element_by_name(container, "rootfiles");
  if (!rootfiles)
  {
    ESP_LOGE(TAG, "Could not find rootfiles in container.xml");
    return false;
  }

  std::string fallback_path;
  for (auto *rootfile = rootfiles->FirstChildElement(); rootfile; rootfile = rootfile->NextSiblingElement())
  {
    if (!element_name_matches(rootfile->Name(), "rootfile"))
    {
      continue;
    }

    const char *full_path = rootfile->Attribute("full-path");
    if (!full_path || full_path[0] == '\0')
    {
      continue;
    }

    if (fallback_path.empty())
    {
      fallback_path = full_path;
    }

    const char *media_type = rootfile->Attribute("media-type");
    if (media_type && strcmp(media_type, "application/oebps-package+xml") == 0)
    {
      content_opf_file = full_path;
      return true;
    }
  }

  if (!fallback_path.empty())
  {
    content_opf_file = fallback_path;
    ESP_LOGW(TAG, "Falling back to first rootfile: %s", content_opf_file.c_str());
    return true;
  }

  ESP_LOGE(TAG, "Could not locate OPF path in container.xml");
  return false;
}

bool Epub::parse_content_opf(ZipFile &zip, std::string &content_opf_file)
{
  char *contents = (char *)zip.read_file_to_memory(content_opf_file.c_str());
  if (!contents)
  {
    ESP_LOGE(TAG, "Could not read OPF: %s", content_opf_file.c_str());
    return false;
  }

  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(contents);
  free(contents);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGE(TAG, "Error parsing OPF (%s): %s", content_opf_file.c_str(), doc.ErrorIDToName(result));
    return false;
  }

  auto *package = find_child_element_by_name(doc.RootElement(), "package");
  if (!package)
  {
    package = doc.RootElement();
  }
  if (!package)
  {
    ESP_LOGE(TAG, "Could not find OPF package root");
    return false;
  }

  auto *metadata = find_child_element_by_name(package, "metadata");
  if (metadata)
  {
    const char *title = find_child_text_by_name(metadata, "title");
    if (title && title[0] != '\0')
    {
      m_title = title;
    }

    const char *cover_item = nullptr;
    for (auto *meta = metadata->FirstChildElement(); meta; meta = meta->NextSiblingElement())
    {
      if (!element_name_matches(meta->Name(), "meta"))
      {
        continue;
      }
      const char *name = meta->Attribute("name");
      if (name && strcmp(name, "cover") == 0)
      {
        cover_item = meta->Attribute("content");
        break;
      }
    }

    auto *manifest = find_child_element_by_name(package, "manifest");
    if (!manifest)
    {
      ESP_LOGE(TAG, "Missing manifest in OPF");
      return false;
    }

    std::map<std::string, std::string> items;
    for (auto *item = manifest->FirstChildElement(); item; item = item->NextSiblingElement())
    {
      if (!element_name_matches(item->Name(), "item"))
      {
        continue;
      }

      const char *item_id = item->Attribute("id");
      const char *href_attr = item->Attribute("href");
      if (!item_id || !href_attr)
      {
        continue;
      }

      std::string href = m_base_path + href_attr;
      items[item_id] = href;

      if (cover_item && strcmp(item_id, cover_item) == 0)
      {
        m_cover_image_item = href;
      }

      const char *media_type = item->Attribute("media-type");
      if ((item_id && strcmp(item_id, "ncx") == 0) ||
          (media_type && strcmp(media_type, "application/x-dtbncx+xml") == 0))
      {
        m_toc_ncx_item = href;
      }
    }

    auto *spine = find_child_element_by_name(package, "spine");
    if (!spine)
    {
      ESP_LOGE(TAG, "Missing spine in OPF");
      return false;
    }

    for (auto *itemref = spine->FirstChildElement(); itemref; itemref = itemref->NextSiblingElement())
    {
      if (!element_name_matches(itemref->Name(), "itemref"))
      {
        continue;
      }
      const char *id = itemref->Attribute("idref");
      if (!id)
      {
        continue;
      }
      auto it = items.find(id);
      if (it != items.end())
      {
        m_spine.push_back(std::make_pair(id, it->second));
      }
    }
  }
  else
  {
    ESP_LOGW(TAG, "Missing metadata in OPF");

    auto *manifest = find_child_element_by_name(package, "manifest");
    if (!manifest)
    {
      ESP_LOGE(TAG, "Missing manifest in OPF");
      return false;
    }

    std::map<std::string, std::string> items;
    for (auto *item = manifest->FirstChildElement(); item; item = item->NextSiblingElement())
    {
      if (!element_name_matches(item->Name(), "item"))
      {
        continue;
      }
      const char *item_id = item->Attribute("id");
      const char *href_attr = item->Attribute("href");
      if (!item_id || !href_attr)
      {
        continue;
      }
      items[item_id] = m_base_path + href_attr;
    }

    auto *spine = find_child_element_by_name(package, "spine");
    if (!spine)
    {
      ESP_LOGE(TAG, "Missing spine in OPF");
      return false;
    }
    for (auto *itemref = spine->FirstChildElement(); itemref; itemref = itemref->NextSiblingElement())
    {
      if (!element_name_matches(itemref->Name(), "itemref"))
      {
        continue;
      }
      const char *id = itemref->Attribute("idref");
      if (!id)
      {
        continue;
      }
      auto it = items.find(id);
      if (it != items.end())
      {
        m_spine.push_back(std::make_pair(id, it->second));
      }
    }
  }

  if (m_title.empty())
  {
    m_title = m_path;
  }

  if (m_spine.empty())
  {
    ESP_LOGE(TAG, "OPF spine is empty");
    return false;
  }

  return true;
}

bool Epub::parse_toc_ncx_file(ZipFile &zip)
{
  m_toc.clear();

  if (m_toc_ncx_item.empty())
  {
    return true;
  }

  char *ncx_data = (char *)zip.read_file_to_memory(m_toc_ncx_item.c_str());
  if (!ncx_data)
  {
    ESP_LOGW(TAG, "Could not find NCX: %s", m_toc_ncx_item.c_str());
    return false;
  }

  tinyxml2::XMLDocument doc;
  auto result = doc.Parse(ncx_data);
  free(ncx_data);
  if (result != tinyxml2::XML_SUCCESS)
  {
    ESP_LOGW(TAG, "Error parsing NCX: %s", doc.ErrorIDToName(result));
    return false;
  }

  auto *ncx = find_child_element_by_name(doc.RootElement(), "ncx");
  if (!ncx)
  {
    ncx = doc.RootElement();
  }
  if (!ncx)
  {
    return false;
  }

  auto *nav_map = find_child_element_by_name(ncx, "navMap");
  if (!nav_map)
  {
    return false;
  }

  for (auto *nav_point = nav_map->FirstChildElement(); nav_point; nav_point = nav_point->NextSiblingElement())
  {
    if (!element_name_matches(nav_point->Name(), "navPoint"))
    {
      continue;
    }

    std::string title;
    auto *nav_label = find_child_element_by_name(nav_point, "navLabel");
    if (nav_label)
    {
      const char *text = find_child_text_by_name(nav_label, "text");
      if (text)
      {
        title = text;
      }
    }

    auto *content = find_child_element_by_name(nav_point, "content");
    const char *src = content ? content->Attribute("src") : nullptr;
    if (!src)
    {
      continue;
    }

    std::string href = m_base_path + src;
    std::string anchor;
    size_t pos = href.find('#');
    if (pos != std::string::npos)
    {
      anchor = href.substr(pos + 1);
      href = href.substr(0, pos);
    }

    m_toc.push_back(EpubTocEntry(title, href, anchor, 0));
  }

  return true;
}

Epub::Epub(const std::string &path) : m_path(path)
{
}

bool Epub::load()
{
  m_title.clear();
  m_cover_image_item.clear();
  m_toc_ncx_item.clear();
  m_spine.clear();
  m_toc.clear();

  ZipFile zip(m_path.c_str());
  std::string content_opf_file;
  if (!find_content_opf_file(zip, content_opf_file))
  {
    ESP_LOGE(TAG, "Could not find content OPF in %s", m_path.c_str());
    return false;
  }

  size_t slash = content_opf_file.find_last_of('/');
  if (slash == std::string::npos)
  {
    m_base_path.clear();
  }
  else
  {
    m_base_path = content_opf_file.substr(0, slash + 1);
  }

  if (!parse_content_opf(zip, content_opf_file))
  {
    return false;
  }

  if (!parse_toc_ncx_file(zip))
  {
    ESP_LOGW(TAG, "TOC parse failed, continue without TOC");
  }

  return true;
}

const std::string &Epub::get_title()
{
  return m_title;
}

const std::string &Epub::get_cover_image_item()
{
  return m_cover_image_item;
}

static std::string normalise_path(const std::string &path)
{
  std::vector<std::string> components;
  std::string component;

  for (char c : path)
  {
    if (c == '/')
    {
      if (!component.empty())
      {
        if (component == "..")
        {
          if (!components.empty())
          {
            components.pop_back();
          }
        }
        else if (component != ".")
        {
          components.push_back(component);
        }
        component.clear();
      }
    }
    else
    {
      component += c;
    }
  }

  if (!component.empty())
  {
    if (component == "..")
    {
      if (!components.empty())
      {
        components.pop_back();
      }
    }
    else if (component != ".")
    {
      components.push_back(component);
    }
  }

  std::string result;
  for (size_t i = 0; i < components.size(); ++i)
  {
    if (i > 0)
    {
      result += "/";
    }
    result += components[i];
  }
  return result;
}

uint8_t *Epub::get_item_contents(const std::string &item_href, size_t *size)
{
  ZipFile zip(m_path.c_str());
  std::string path = normalise_path(item_href);
  auto *content = zip.read_file_to_memory(path.c_str(), size);
  if (!content)
  {
    ESP_LOGE(TAG, "Failed to read item %s", path.c_str());
    return nullptr;
  }
  return content;
}

int Epub::get_spine_items_count()
{
  return static_cast<int>(m_spine.size());
}

std::string &Epub::get_spine_item(int spine_index)
{
  static std::string empty;
  if (m_spine.empty())
  {
    return empty;
  }

  if (spine_index < 0 || spine_index >= static_cast<int>(m_spine.size()))
  {
    ESP_LOGW(TAG, "Spine index %d out of range, fallback to 0", spine_index);
    spine_index = 0;
  }
  return m_spine[spine_index].second;
}

int Epub::get_spine_item_id(std::string spine_key)
{
  for (int i = 0; i < static_cast<int>(m_spine.size()); ++i)
  {
    if (m_spine[i].first == spine_key || m_spine[i].second == spine_key)
    {
      return i;
    }
  }
  return -1;
}

EpubTocEntry &Epub::get_toc_item(int toc_index)
{
  static EpubTocEntry empty_toc("", "", "", 0);
  if (m_toc.empty())
  {
    return empty_toc;
  }
  if (toc_index < 0 || toc_index >= static_cast<int>(m_toc.size()))
  {
    return m_toc[0];
  }
  return m_toc[toc_index];
}

int Epub::get_toc_items_count()
{
  return static_cast<int>(m_toc.size());
}

int Epub::get_spine_index_for_toc_index(int toc_index)
{
  if (toc_index < 0 || toc_index >= static_cast<int>(m_toc.size()))
  {
    return 0;
  }

  for (int i = 0; i < static_cast<int>(m_spine.size()); ++i)
  {
    if (m_spine[i].second == m_toc[toc_index].href)
    {
      return i;
    }
  }

  return 0;
}
