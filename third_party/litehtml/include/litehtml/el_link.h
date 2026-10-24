#ifndef LH_EL_LINK_H
#define LH_EL_LINK_H

#include "html_tag.h"

namespace litehtml
{
	class el_link : public html_tag
	{
	public:
		explicit el_link(const std::shared_ptr<litehtml::document>& doc);

		bool m_processed = false;  // BrowseNX patch
	protected:
		void parse_attributes() override;
	};
}

#endif  // LH_EL_LINK_H
