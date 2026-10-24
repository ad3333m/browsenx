"""Applies BrowseNX's JavaScript-DOM patches to the vendored litehtml (third_party/litehtml).

Idempotent: run from the repo root after re-vendoring litehtml. Every patched spot is marked
"BrowseNX patch" so it can be found again.
"""
import pathlib
import sys

ROOT = pathlib.Path("third_party/litehtml")
MARK = "BrowseNX patch"


def patch(rel, old, new, count=1):
    p = ROOT / rel
    s = p.read_text(encoding="utf-8")
    if new in s:
        return
    if s.count(old) != count:
        sys.exit(f"{rel}: expected {count} match(es) of:\n{old}")
    p.write_text(s.replace(old, new), encoding="utf-8", newline="\n")


# element: insertBefore, attribute access, style reset
patch("include/litehtml/element.h",
      "\t\tvirtual void\t\t\t\tclearRecursive();\n",
      "\t\tvirtual void\t\t\t\tclearRecursive();\n"
      f"\t\t// {MARK}: DOM mutation for JavaScript\n"
      "\t\tvirtual bool\t\t\t\tinsertBefore(const ptr& el, const ptr& before);\n"
      "\t\tvirtual void\t\t\t\tremove_attr(const char* /*name*/) {}\n"
      "\t\tvirtual const string_map*\tattrs() const { return nullptr; }\n"
      "\t\tvirtual void\t\t\t\treset_styles();\n")

patch("src/element.cpp",
      "bool element::appendChild(const ptr &/*el*/)",
      f"// {MARK}\n"
      "bool element::insertBefore(const ptr& el, const ptr& before)\n"
      "{\n"
      "\tif (!el) return false;\n"
      "\tauto it = m_children.begin();\n"
      "\twhile (it != m_children.end() && *it != before) ++it;\n"
      "\tm_children.insert(it, el);\n"
      "\tel->parent(shared_from_this());\n"
      "\treturn true;\n"
      "}\n\n"
      "void element::reset_styles()\n"
      "{\n"
      "\tm_used_styles.clear();\n"
      "\tfor (auto& el : m_children) el->reset_styles();\n"
      "}\n\n"
      "bool element::appendChild(const ptr &/*el*/)")

patch("include/litehtml/html_tag.h",
      "\t\tvoid\t\t\t\tclearRecursive() override;\n",
      "\t\tvoid\t\t\t\tclearRecursive() override;\n"
      f"\t\t// {MARK}\n"
      "\t\tvoid\t\t\t\tremove_attr(const char* name) override;\n"
      "\t\tconst string_map*\tattrs() const override { return &m_attrs; }\n"
      "\t\tvoid\t\t\t\treset_styles() override;\n")

patch("src/html_tag.cpp",
      "void litehtml::html_tag::clearRecursive()",
      f"// {MARK}\n"
      "void litehtml::html_tag::remove_attr(const char* name)\n"
      "{\n"
      "\tif (!name) return;\n"
      "\tstring n = lowcase(name);\n"
      "\tm_attrs.erase(n);\n"
      "\tif (n == \"class\") { m_str_classes.clear(); m_classes.clear(); }\n"
      "\telse if (n == \"id\") m_id = empty_id;\n"
      "}\n\n"
      "void litehtml::html_tag::reset_styles()\n"
      "{\n"
      "\tm_style.clear();\n"
      "\telement::reset_styles();\n"
      "}\n\n"
      "void litehtml::html_tag::clearRecursive()")

# <style> and <link> add their sheet once, so a restyle can re-run parse_attributes
patch("include/litehtml/el_style.h",
      "\t\telements_list\t\tm_children;\n",
      "\t\telements_list\t\tm_children;\n"
      f"\t\tbool\t\t\t\tm_parsed = false;  // {MARK}\n")
patch("src/el_style.cpp",
      "void litehtml::el_style::parse_attributes()\n{\n",
      "void litehtml::el_style::parse_attributes()\n{\n"
      f"\tif (m_parsed) return;  // {MARK}\n"
      "\tm_parsed = true;\n")
patch("include/litehtml/el_link.h",
      "\tprotected:\n\t\tvoid parse_attributes() override;\n",
      f"\t\tbool m_processed = false;  // {MARK}\n"
      "\tprotected:\n\t\tvoid parse_attributes() override;\n")
patch("src/el_link.cpp",
      "\tbool processed = false;\n",
      f"\tif (m_processed) return;  // {MARK}: the sheet is in already\n"
      "\tbool processed = false;\n")
patch("src/el_link.cpp",
      "\tif(!processed)\n\t{\n\t\tdoc->container()->link(doc, shared_from_this());\n\t}\n",
      "\tm_processed = processed;\n"
      "\tif(!processed)\n\t{\n\t\tdoc->container()->link(doc, shared_from_this());\n\t}\n")

# document: fragment parsing and a full restyle
patch("include/litehtml/document.h",
      "\t\tvoid\t\t\t\t\t\t\tappend_children_from_string(element& parent, const char* str, bool replace_existing);\n",
      "\t\tvoid\t\t\t\t\t\t\tappend_children_from_string(element& parent, const char* str, bool replace_existing);\n"
      f"\t\t// {MARK}: innerHTML and re-applying CSS after scripts change the tree\n"
      "\t\telements_list\t\t\t\t\tparse_fragment(const char* html);\n"
      "\t\tvoid\t\t\t\t\t\t\trestyle();\n")
patch("include/litehtml/document.h",
      "\t\tcss_text::vector\t\t\t\t\tm_css;\n",
      "\t\tcss_text::vector\t\t\t\t\tm_css;\n"
      f"\t\tsize_t\t\t\t\t\t\t\tm_css_parsed = 0;  // {MARK}\n")
patch("src/document.cpp",
      "\t\t// Sort css selectors using CSS rules.\n\t\tdoc->m_styles.sort_selectors();\n",
      "\t\t// Sort css selectors using CSS rules.\n\t\tdoc->m_styles.sort_selectors();\n"
      f"\t\tdoc->m_css_parsed = doc->m_css.size();  // {MARK}\n")
patch("src/document.cpp",
      "void document::append_children_from_string(element& parent, const char* str, bool replace_existing)",
      f"// {MARK}\n"
      "elements_list document::parse_fragment(const char* str)\n"
      "{\n"
      "\tGumboOptions opts = kGumboDefaultOptions;\n"
      "\topts.fragment_context = GUMBO_TAG_BODY;\n"
      "\tGumboOutput* output = gumbo_parse_with_options(&opts, str, strlen(str));\n"
      "\telements_list child_elements;\n"
      "\tcreate_node(output->root, child_elements, true, false);\n"
      "\tgumbo_destroy_output(&kGumboDefaultOptions, output);\n"
      "\treturn child_elements;\n"
      "}\n\n"
      "void document::restyle()\n"
      "{\n"
      "\tm_root->reset_styles();\n"
      "\tm_root->apply_stylesheet(m_master_css);\n"
      "\tm_root->parse_attributes();  // also picks up <style>/<link> added since last time\n"
      "\tif (m_css.size() > m_css_parsed)\n"
      "\t{\n"
      "\t\tfor (size_t i = m_css_parsed; i < m_css.size(); i++)\n"
      "\t\t{\n"
      "\t\t\tconst auto& css = m_css[i];\n"
      "\t\t\tmedia_query_list_list::ptr media;\n"
      "\t\t\tif (css.media != \"\")\n"
      "\t\t\t{\n"
      "\t\t\t\tauto mq_list = parse_media_query_list(css.media, shared_from_this());\n"
      "\t\t\t\tmedia = make_shared<media_query_list_list>();\n"
      "\t\t\t\tmedia->add(mq_list);\n"
      "\t\t\t}\n"
      "\t\t\tm_styles.parse_css_stylesheet(css.text, css.baseurl, shared_from_this(), media);\n"
      "\t\t}\n"
      "\t\tm_css_parsed = m_css.size();\n"
      "\t\tm_styles.sort_selectors();\n"
      "\t\tupdate_media_lists(m_media);\n"
      "\t}\n"
      "\tm_root->apply_stylesheet(m_styles);\n"
      "\tm_root->apply_stylesheet(m_user_css);\n"
      "\tm_root->compute_styles();\n"
      "\tm_tabular_elements.clear();\n"
      "\tm_root_render = m_root->create_render_item(nullptr);\n"
      "\tfix_tables_layout();\n"
      "\tif (m_root_render) m_root_render = m_root_render->init();\n"
      "}\n\n"
      "void document::append_children_from_string(element& parent, const char* str, bool replace_existing)")

print("litehtml patched")
