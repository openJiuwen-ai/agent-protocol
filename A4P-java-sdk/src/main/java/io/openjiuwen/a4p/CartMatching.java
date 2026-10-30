package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Each execution buys exactly one product per disjoint whitelist group, at its declared quantity.
final class CartMatching {
    private CartMatching() {}

    static boolean matches(ItemWhiteList group, Product product) {
        return group.whiteList().contains(product);
    }

    static void requireOneToOne(List<CartItem> items, List<ItemWhiteList> groups) {
        // Scope is checked before template shape, matching ArkTS error precedence.
        for (CartItem item : items) {
            Validation.require(groups.stream().anyMatch(group -> matches(group, item.product())),
                    A4P_PRODUCT_NOT_AUTHORIZED);
        }
        Validation.require(items.size() == groups.size(), A4P_CART_MISMATCH);
        Set<Integer> matched = new HashSet<>();
        for (CartItem item : items) {
            int index = 0;
            while (!matches(groups.get(index), item.product())) { index++; }
            Validation.require(matched.add(index), A4P_CART_MISMATCH);
            Validation.require(item.quantity().equals(groups.get(index).quantity()), A4P_CART_MISMATCH);
        }
    }
}
