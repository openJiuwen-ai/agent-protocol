package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.util.HashSet;
import java.util.List;
import java.util.Set;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Each execution buys exactly one product per disjoint whitelist group, at its declared quantity.
/**
 * Validates exact one-to-one matching between cart entries and disjoint whitelist groups.
 * Each execution chooses one authorized product from every group at that group quantity.
 */
final class CartMatching {
    /**
     * Prevents instantiation of this utility class.
     */
    private CartMatching() {}

    /**
     * Checks exact product membership, including seller and SPU/SKU identity, in a whitelist group.
     * @param group authorized alternative-product group
     * @param product seller and SPU/SKU product identity
     * @return true if the exact product is in the group
     */
    static boolean matches(ItemWhiteList group, Product product) {
        return group.whiteList().contains(product);
    }

    /**
     * Requires one cart product from each disjoint group with its exact template quantity.
     * Checks unauthorized products before cart shape to preserve protocol error precedence.
     * @param items cart entries to validate
     * @param groups disjoint authorized product groups
     */
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
