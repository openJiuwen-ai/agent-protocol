package io.openjiuwen.a4p;

import io.openjiuwen.a4p.Models.*;
import java.util.List;
import org.junit.jupiter.api.Test;
import static io.openjiuwen.a4p.A4pErrorCode.*;
import static io.openjiuwen.a4p.Fixtures.*;
import static org.junit.jupiter.api.Assertions.*;

class CartMatchingTest {
    private static final Product A = product("a");
    private static final Product B = product("b");
    private static final Product C = product("c");

    private static Product product(String sku) {
        return new Product(new SellerInfo("seller"), new SpuSkuInfo(null, sku));
    }

    private static ItemWhiteList group(Product... products) {
        return new ItemWhiteList(List.of(products), 1L);
    }

    private static List<CartItem> cart(Product... products) {
        return java.util.Arrays.stream(products).map(product -> new CartItem(product, 1L)).toList();
    }

    @Test
    void acceptsAnyCartOrder() {
        List<ItemWhiteList> groups = List.of(group(A), group(B), group(C));
        assertDoesNotThrow(() -> CartMatching.requireOneToOne(cart(A, B, C), groups));
        assertDoesNotThrow(() -> CartMatching.requireOneToOne(cart(C, A, B), groups));
        assertDoesNotThrow(() -> CartMatching.requireOneToOne(cart(B, C, A), groups));
    }

    @Test
    void rejectsCartsWithoutAOneToOneMatching() {
        thrown(A4P_CART_MISMATCH, () -> CartMatching.requireOneToOne(cart(A, A), List.of(group(A), group(B))));
        thrown(A4P_PRODUCT_NOT_AUTHORIZED, () -> CartMatching.requireOneToOne(cart(C), List.of(group(A, B))));
        thrown(A4P_CART_MISMATCH,
                () -> CartMatching.requireOneToOne(cart(A, B, A), List.of(group(A, B), group(A, B), group(A))));
    }

    @Test
    void rejectsCartsWhoseSizeDiffersFromTheWhitelist() {
        thrown(A4P_CART_MISMATCH, () -> CartMatching.requireOneToOne(cart(A), List.of(group(A), group(B))));
        thrown(A4P_CART_MISMATCH, () -> CartMatching.requireOneToOne(cart(A, B), List.of(group(A, B))));
    }
}
