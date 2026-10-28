package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.math.BigInteger;
import java.util.Arrays;
import java.util.List;
import static io.openjiuwen.a4p.A4pErrorCode.*;

// Whitelist groups are ANDed: every group is bought through its own cart item, in any cart order.
final class CartMatching {
    private CartMatching() {}

    // Requires a one-to-one assignment of cart items to groups whose whitelist contains the item's product.
    static void requireOneToOne(List<CartItem> items, List<ItemWhiteList> groups) {
        Validation.require(items.size() == groups.size(), A4P_INVALID_PARAMETER);
        int[] owner = new int[groups.size()];
        Arrays.fill(owner, -1);
        for (int item = 0; item < items.size(); item++) {
            Validation.require(assign(item, items, groups, owner, new boolean[groups.size()]),
                    A4P_PRODUCT_NOT_AUTHORIZED);
        }
    }

    static boolean matches(ItemWhiteList group, Product product) {
        return group.whiteList().stream().anyMatch(product::equals);
    }

    static BigInteger quantity(List<CartItem> items, ItemWhiteList group) {
        return items.stream().filter(item -> matches(group, item.product()))
                .map(item -> BigInteger.valueOf(item.quantity())).reduce(BigInteger.ZERO, BigInteger::add);
    }

    // Augmenting path: a greedy pick can block a later item, so earlier assignments may be moved.
    private static boolean assign(int item, List<CartItem> items, List<ItemWhiteList> groups, int[] owner,
                                  boolean[] visited) {
        for (int group = 0; group < groups.size(); group++) {
            if (visited[group] || !matches(groups.get(group), items.get(item).product())) { continue; }
            visited[group] = true;
            if (owner[group] < 0 || assign(owner[group], items, groups, owner, visited)) {
                owner[group] = item;
                return true;
            }
        }
        return false;
    }
}
