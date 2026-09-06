#pragma once

// Anonymized 19-window dry-run layout: full coverage but a distant recent peer.
inline solver::LayoutSnapshot distant_crowded_desktop_fixture()
{
    const geometry::Rect visuals[] = {
        {841, 302, 3000, 1811},
        {1914, 254, 3822, 1684},
        {841, 254, 2986, 1873},
        {894, 206, 3125, 1666},
        {1806, 158, 3708, 1380},
        {185, 1028, 1497, 2009},
        {958, 158, 2653, 1113},
        {958, 110, 3366, 1601},
        {1806, 62, 3118, 1015},
        {1806, 14, 3215, 1034},
        {958, 62, 2653, 1017},
        {958, 14, 2653, 969},
        {185, 980, 1497, 1961},
        {185, 932, 1497, 1913},
        {136, 302, 1545, 1322},
        {46, 206, 1741, 1161},
        {110, 158, 1805, 1113},
        {192, 110, 1723, 1427},
        {110, 55, 1805, 1010},
    };
    const geometry::Rect frames[] = {
        {834, 302, 3007, 1818},
        {1914, 254, 3822, 1684},
        {834, 254, 2993, 1880},
        {887, 206, 3132, 1673},
        {1799, 158, 3715, 1387},
        {178, 1028, 1504, 2016},
        {951, 158, 2660, 1120},
        {951, 110, 3373, 1608},
        {1799, 62, 3125, 1022},
        {1801, 14, 3220, 1039},
        {951, 62, 2660, 1024},
        {951, 14, 2660, 976},
        {178, 980, 1504, 1968},
        {178, 932, 1504, 1920},
        {131, 302, 1550, 1327},
        {39, 206, 1748, 1168},
        {103, 158, 1812, 1120},
        {185, 110, 1730, 1434},
        {103, 55, 1812, 1017},
    };
    solver::LayoutSnapshot result;
    for (std::size_t i = 0; i < std::size(visuals); ++i) {
        auto item = window(i + 1, visuals[i], static_cast<int>(i));
        item.placementRect = item.lastStableRect = frames[i];
        item.workArea = {0, 0, 3840, 2112};
        item.titleBarHeight = 31;
        result.windows.push_back(item);
    }
    return result;
}

// Same desktop before optimization; old lower-window boundaries can crowd
// useful candidate coordinates out of a truncated grid.
inline solver::LayoutSnapshot activation_crowded_desktop_fixture()
{
    const geometry::Rect visuals[] = {
        {841, 302, 3000, 1811},
        {1932, 254, 3840, 1684},
        {859, 254, 3004, 1873},
        {1602, 206, 3833, 1666},
        {1931, 158, 3833, 1380},
        {185, 1124, 1497, 2105},
        {1083, 158, 2778, 1113},
        {1425, 110, 3833, 1601},
        {185, 1076, 1497, 2029},
        {136, 1028, 1545, 2048},
        {2138, 62, 3833, 1017},
        {1290, 62, 2985, 1017},
        {185, 980, 1497, 1961},
        {185, 932, 1497, 1913},
        {136, 884, 1545, 1904},
        {2098, 14, 3793, 969},
        {1250, 14, 2945, 969},
        {75, 788, 1606, 2105},
        {46, 55, 1741, 1010},
    };
    const geometry::Rect frames[] = {
        {834, 302, 3007, 1818},
        {1932, 254, 3840, 1684},
        {852, 254, 3011, 1880},
        {1595, 206, 3840, 1673},
        {1924, 158, 3840, 1387},
        {178, 1124, 1504, 2112},
        {1076, 158, 2785, 1120},
        {1418, 110, 3840, 1608},
        {178, 1076, 1504, 2036},
        {131, 1028, 1550, 2053},
        {2131, 62, 3840, 1024},
        {1283, 62, 2992, 1024},
        {178, 980, 1504, 1968},
        {178, 932, 1504, 1920},
        {131, 884, 1550, 1909},
        {2091, 14, 3800, 976},
        {1243, 14, 2952, 976},
        {68, 788, 1613, 2112},
        {39, 55, 1748, 1017},
    };
    solver::LayoutSnapshot result;
    for (std::size_t i = 0; i < std::size(visuals); ++i) {
        auto item = window(i + 1, visuals[i], static_cast<int>(i));
        item.placementRect = item.lastStableRect = frames[i];
        item.workArea = {0, 0, 3840, 2112};
        item.titleBarHeight = 31;
        result.windows.push_back(item);
    }
    return result;
}
