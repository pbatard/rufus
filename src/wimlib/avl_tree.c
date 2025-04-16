/*
 * avl_tree.c - intrusive, nonrecursive AVL tree data structure (self-balancing
 *		binary search tree), implementation file
 *
 * Copyright 2022 Eric Biggers
 *
 * Permission is hereby granted, free of charge, to any person
 * obtaining a copy of this software and associated documentation
 * files (the "Software"), to deal in the Software without
 * restriction, including without limitation the rights to use,
 * copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following
 * conditions:
 *
 * The above copyright notice and this permission notice shall be
 * included in all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES
 * OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT
 * HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY,
 * WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
 * FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */

#ifdef HAVE_CONFIG_H
#  include "config.h"
#endif

#include "wimlib/avl_tree.h"

/* Returns the left child (sign < 0) or the right child (sign > 0) of the
 * specified AVL tree node.
 * Note: for all calls of this, 'sign' is constant at compilation time,
 * so the compiler can remove the conditional.  */
static AVL_INLINE struct avl_tree_node *
avl_get_child(const struct avl_tree_node *parent, int sign)
{
	if (sign < 0)
		return parent->left;
	else
		return parent->right;
}

static AVL_INLINE struct avl_tree_node *
avl_tree_first_or_last_in_order(const struct avl_tree_node *root, int sign)
{
	const struct avl_tree_node *first = root;

	if (first)
		while (avl_get_child(first, +sign))
			first = avl_get_child(first, +sign);
	return (struct avl_tree_node *)first;
}

/* Starts an in-order traversal of the tree: returns the least-valued node, or
 * NULL if the tree is empty.  */
struct avl_tree_node *
avl_tree_first_in_order(const struct avl_tree_node *root)
{
	return avl_tree_first_or_last_in_order(root, -1);
}

/* Starts a *reverse* in-order traversal of the tree: returns the
 * greatest-valued node, or NULL if the tree is empty.  */
struct avl_tree_node *
avl_tree_last_in_order(const struct avl_tree_node *root)
{
	return avl_tree_first_or_last_in_order(root, 1);
}

static AVL_INLINE struct avl_tree_node *
avl_tree_next_or_prev_in_order(const struct avl_tree_node *node, int sign)
{
	const struct avl_tree_node *next;

	if (avl_get_child(node, +sign))
		for (next = avl_get_child(node, +sign);
		     avl_get_child(next, -sign);
		     next = avl_get_child(next, -sign))
			;
	else
		for (next = avl_get_parent(node);
		     next && node == avl_get_child(next, +sign);
		     node = next, next = avl_get_parent(next))
			;
	return (struct avl_tree_node *)next;
}

/* Continues an in-order traversal of the tree: returns the next-greatest-valued
 * node, or NULL if there is none.  */
struct avl_tree_node *
avl_tree_next_in_order(const struct avl_tree_node *node)
{
	return avl_tree_next_or_prev_in_order(node, 1);
}

/* Continues a *reverse* in-order traversal of the tree: returns the
 * previous-greatest-valued node, or NULL if there is none.  */
struct avl_tree_node *
avl_tree_prev_in_order(const struct avl_tree_node *node)
{
	return avl_tree_next_or_prev_in_order(node, -1);
}

/* Starts a postorder traversal of the tree.  */
struct avl_tree_node *
avl_tree_first_in_postorder(const struct avl_tree_node *root)
{
	const struct avl_tree_node *first = root;

	if (first)
		while (first->left || first->right)
			first = first->left ? first->left : first->right;

	return (struct avl_tree_node *)first;
}

/* Continues a postorder traversal of the tree.  @prev will not be deferenced as
 * it's allowed that its memory has been freed; @prev_parent must be its saved
 * parent node.  Returns NULL if there are no more nodes (i.e. @prev was the
 * root of the tree).  */
struct avl_tree_node *
avl_tree_next_in_postorder(const struct avl_tree_node *prev,
			   const struct avl_tree_node *prev_parent)
{
	const struct avl_tree_node *next = prev_parent;

	if (next && prev == next->left && next->right)
		for (next = next->right;
		     next->left || next->right;
		     next = next->left ? next->left : next->right)
			;
	return (struct avl_tree_node *)next;
}

/* Sets the left child (sign < 0) or the right child (sign > 0) of the
 * specified AVL tree node.
 * Note: for all calls of this, 'sign' is constant at compilation time,
 * so the compiler can remove the conditional.  */
static AVL_INLINE void
avl_set_child(struct avl_tree_node *parent, int sign,
	      struct avl_tree_node *child)
{
	if (sign < 0)
		parent->left = child;
	else
		parent->right = child;
}

/* Sets the parent and balance factor of the specified AVL tree node.  */
static AVL_INLINE void
avl_set_parent_balance(struct avl_tree_node *node, struct avl_tree_node *parent,
		       int balance_factor)
{
	node->parent_balance = (uintptr_t)parent | (balance_factor + 1);
}

/* Sets the parent of the specified AVL tree node.  */
static AVL_INLINE void
avl_set_parent(struct avl_tree_node *node, struct avl_tree_node *parent)
{
	node->parent_balance = (uintptr_t)parent | (node->parent_balance & 3);
}

/* Returns the balance factor of the specified AVL tree node --- that is, the
 * height of its right subtree minus the height of its left subtree.  */
static AVL_INLINE int
avl_get_balance_factor(const struct avl_tree_node *node)
{
	return (int)(node->parent_balance & 3) - 1;
}

/* Adds @amount to the balance factor of the specified AVL tree node.
 * The caller must ensure this still results in a valid balance factor
 * (-1, 0, or 1).  */
static AVL_INLINE void
avl_adjust_balance_factor(struct avl_tree_node *node, int amount)
{
	node->parent_balance += amount;
}

static AVL_INLINE void
avl_replace_child(struct avl_tree_node **root_ptr,
		  struct avl_tree_node *parent,
		  struct avl_tree_node *old_child,
		  struct avl_tree_node *new_child)
{
	if (parent) {
		if (old_child == parent->left)
			parent->left = new_child;
		else
			parent->right = new_child;
	} else {
		*root_ptr = new_child;
	}
}

/*
 * Template for performing a single rotation ---
 *
 * sign > 0:  Rotate clockwise (right) rooted at A:
 *
 *           P?            P?
 *           |             |
 *           A             B
 *          / \           / \
 *         B   C?  =>    D?  A
 *        / \               / \
 *       D?  E?            E?  C?
 *
 * (nodes marked with ? may not exist)
 *
 * sign < 0:  Rotate counterclockwise (left) rooted at A:
 *
 *           P?            P?
 *           |             |
 *           A             B
 *          / \           / \
 *         C?  B   =>    A   D?
 *            / \       / \
 *           E?  D?    C?  E?
 *
 * This updates pointers but not balance factors!
 */
static AVL_INLINE void
avl_rotate(struct avl_tree_node ** const root_ptr,
	   struct avl_tree_node * const A, const int sign)
{
	struct avl_tree_node * const B = avl_get_child(A, -sign);
	struct avl_tree_node * const E = avl_get_child(B, +sign);
	struct avl_tree_node * const P = avl_get_parent(A);

	avl_set_child(A, -sign, E);
	avl_set_parent(A, B);

	avl_set_child(B, +sign, A);
	avl_set_parent(B, P);

	if (E)
		avl_set_parent(E, A);

	avl_replace_child(root_ptr, P, A, B);
}

/*
 * Template for performing a double rotation ---
 *
 * sign > 0:  Rotate counterclockwise (left) rooted at B, then
 *		     clockwise (right) rooted at A:
 *
 *           P?            P?          P?
 *           |             |           |
 *           A             A           E
 *          / \           / \        /   \
 *         B   C?  =>    E   C? =>  B     A
 *        / \           / \        / \   / \
 *       D?  E         B   G?     D?  F?G?  C?
 *          / \       / \
 *         F?  G?    D?  F?
 *
 * (nodes marked with ? may not exist)
 *
 * sign < 0:  Rotate clockwise (right) rooted at B, then
 *		     counterclockwise (left) rooted at A:
 *
 *         P?          P?              P?
 *         |           |               |
 *         A           A               E
 *        / \         / \            /   \
 *       C?  B   =>  C?  E    =>    A     B
 *          / \         / \        / \   / \
 *         E   D?      G?  B      C?  G?F?  D?
 *        / \             / \
 *       G?  F?          F?  D?
 *
 * Returns a pointer to E and updates balance factors.  Except for those
 * two things, this function is equivalent to:
 *	avl_rotate(root_ptr, B, -sign);
 *	avl_rotate(root_ptr, A, +sign);
 *
 * See comment in avl_handle_subtree_growth() for explanation of balance
 * factor updates.
 */
static AVL_INLINE struct avl_tree_node *
avl_do_double_rotate(struct avl_tree_node ** const root_ptr,
		     struct avl_tree_node * const B,
		     struct avl_tree_node * const A, const int sign)
{
	struct avl_tree_node * const E = avl_get_child(B, +sign);
	struct avl_tree_node * const F = avl_get_child(E, -sign);
	struct avl_tree_node * const G = avl_get_child(E, +sign);
	struct avl_tree_node * const P = avl_get_parent(A);
	const int e = avl_get_balance_factor(E);

	avl_set_child(A, -sign, G);
	avl_set_parent_balance(A, E, ((sign * e >= 0) ? 0 : -e));

	avl_set_child(B, +sign, F);
	avl_set_parent_balance(B, E, ((sign * e <= 0) ? 0 : -e));

	avl_set_child(E, +sign, A);
	avl_set_child(E, -sign, B);
	avl_set_parent_balance(E, P, 0);

	if (G)
		avl_set_parent(G, A);

	if (F)
		avl_set_parent(F, B);

	avl_replace_child(root_ptr, P, A, E);

	return E;
}

/*
 * This function handles the growth of a subtree due to an insertion.
 *
 * @root_ptr
 *	Location of the tree's root pointer.
 *
 * @node
 *	A subtree that has increased in height by 1 due to an insertion.
 *
 * @parent
 *	Parent of @node; must not be NULL.
 *
 * @sign
 *	-1 if @node is the left child of @parent;
 *	+1 if @node is the right child of @parent.
 *
 * This function will adjust @parent's balance factor, then do a (single
 * or double) rotation if necessary.  The return value will be %true if
 * the full AVL tree is now adequately balanced, or %false if the subtree
 * rooted at @parent is now adequately balanced but has increased in
 * height by 1, so the caller should continue up the tree.
 *
 * Note that if %false is returned, no rotation will have been done.
 * Indeed, a single node insertion cannot require that more than one
 * (single or double) rotation be done.
 */
static AVL_INLINE bool
avl_handle_subtree_growth(struct avl_tree_node ** const root_ptr,
			  struct avl_tree_node * const node,
			  struct avl_tree_node * const parent,
			  const int sign)
{
	int old_balance_factor, new_balance_factor;

	old_balance_factor = avl_get_balance_factor(parent);

	if (old_balance_factor == 0) {
		avl_adjust_balance_factor(parent, sign);
		/* @parent is still sufficiently balanced (-1 or +1
		 * balance factor), but must have increased in height.
		 * Continue up the tree.  */
		return false;
	}

	new_balance_factor = old_balance_factor + sign;

	if (new_balance_factor == 0) {
		avl_adjust_balance_factor(parent, sign);
		/* @parent is now perfectly balanced (0 balance factor).
		 * It cannot have increased in height, so there is
		 * nothing more to do.  */
		return true;
	}

	/* @parent is too left-heavy (new_balance_factor == -2) or
	 * too right-heavy (new_balance_factor == +2).  */

	/* Test whether @node is left-heavy (-1 balance factor) or
	 * right-heavy (+1 balance factor).
	 * Note that it cannot be perfectly balanced (0 balance factor)
	 * because here we are under the invariant that @node has
	 * increased in height due to the insertion.  */
	if (sign * avl_get_balance_factor(node) > 0) {

		/* @node (B below) is heavy in the same direction @parent
		 * (A below) is heavy.
		 *
		 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		 * The comment, diagram, and equations below assume sign < 0.
		 * The other case is symmetric!
		 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		 *
		 * Do a clockwise rotation rooted at @parent (A below):
		 *
		 *           A              B
		 *          / \           /   \
		 *         B   C?  =>    D     A
		 *        / \           / \   / \
		 *       D   E?        F?  G?E?  C?
		 *      / \
		 *     F?  G?
		 *
		 * Before the rotation:
		 *	balance(A) = -2
		 *	balance(B) = -1
		 * Let x = height(C).  Then:
		 *	height(B) = x + 2
		 *	height(D) = x + 1
		 *	height(E) = x
		 *	max(height(F), height(G)) = x.
		 *
		 * After the rotation:
		 *	height(D) = max(height(F), height(G)) + 1
		 *		  = x + 1
		 *	height(A) = max(height(E), height(C)) + 1
		 *		  = max(x, x) + 1 = x + 1
		 *	balance(B) = 0
		 *	balance(A) = 0
		 */
		avl_rotate(root_ptr, parent, -sign);

		/* Equivalent to setting @parent's balance factor to 0.  */
		avl_adjust_balance_factor(parent, -sign); /* A */

		/* Equivalent to setting @node's balance factor to 0.  */
		avl_adjust_balance_factor(node, -sign);   /* B */
	} else {
		/* @node (B below) is heavy in the direction opposite
		 * from the direction @parent (A below) is heavy.
		 *
		 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		 * The comment, diagram, and equations below assume sign < 0.
		 * The other case is symmetric!
		 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
		 *
		 * Do a counterblockwise rotation rooted at @node (B below),
		 * then a clockwise rotation rooted at @parent (A below):
		 *
		 *           A             A           E
		 *          / \           / \        /   \
		 *         B   C?  =>    E   C? =>  B     A
		 *        / \           / \        / \   / \
		 *       D?  E         B   G?     D?  F?G?  C?
		 *          / \       / \
		 *         F?  G?    D?  F?
		 *
		 * Before the rotation:
		 *	balance(A) = -2
		 *	balance(B) = +1
		 * Let x = height(C).  Then:
		 *	height(B) = x + 2
		 *	height(E) = x + 1
		 *	height(D) = x
		 *	max(height(F), height(G)) = x
		 *
		 * After both rotations:
		 *	height(A) = max(height(G), height(C)) + 1
		 *		  = x + 1
		 *	balance(A) = balance(E{orig}) >= 0 ? 0 : -balance(E{orig})
		 *	height(B) = max(height(D), height(F)) + 1
		 *		  = x + 1
		 *	balance(B) = balance(E{orig} <= 0) ? 0 : -balance(E{orig})
		 *
		 *	height(E) = x + 2
		 *	balance(E) = 0
		 */
		avl_do_double_rotate(root_ptr, node, parent, -sign);
	}

	/* Height after rotation is unchanged; nothing more to do.  */
	return true;
}

/* Rebalance the tree after insertion of the specified node.  */
void
avl_tree_rebalance_after_insert(struct avl_tree_node **root_ptr,
				struct avl_tree_node *inserted)
{
	struct avl_tree_node *node, *parent;
	bool done;

	inserted->left = NULL;
	inserted->right = NULL;

	node = inserted;

	/* Adjust balance factor of new node's parent.
	 * No rotation will need to be done at this level.  */

	parent = avl_get_parent(node);
	if (!parent)
		return;

	if (node == parent->left)
		avl_adjust_balance_factor(parent, -1);
	else
		avl_adjust_balance_factor(parent, +1);

	if (avl_get_balance_factor(parent) == 0)
		/* @parent did not change in height.  Nothing more to do.  */
		return;

	/* The subtree rooted at @parent increased in height by 1.  */

	do {
		/* Adjust balance factor of next ancestor.  */

		node = parent;
		parent = avl_get_parent(node);
		if (!parent)
			return;

		/* The subtree rooted at @node has increased in height by 1.  */
		if (node == parent->left)
			done = avl_handle_subtree_growth(root_ptr, node,
							 parent, -1);
		else
			done = avl_handle_subtree_growth(root_ptr, node,
							 parent, +1);
	} while (!done);
}

/*
 * This function handles the shrinkage of a subtree due to a deletion.
 *
 * @root_ptr
 *	Location of the tree's root pointer.
 *
 * @parent
 *	A node in the tree, exactly one of whose subtrees has decreased
 *	in height by 1 due to a deletion.  (This includes the case where
 *	one of the child pointers has become NULL, since we can consider
 *	the "NULL" subtree to have a height of 0.)
 *
 * @sign
 *	+1 if the left subtree of @parent has decreased in height by 1;
 *	-1 if the right subtree of @parent has decreased in height by 1.
 *
 * @left_deleted_ret
 *	If the return value is not NULL, this will be set to %true if the
 *	left subtree of the returned node has decreased in height by 1,
 *	or %false if the right subtree of the returned node has decreased
 *	in height by 1.
 *
 * This function will adjust @parent's balance factor, then do a (single
 * or double) rotation if necessary.  The return value will be NULL if
 * the full AVL tree is now adequately balanced, or a pointer to the
 * parent of @parent if @parent is now adequately balanced but has
 * decreased in height by 1.  Also in the latter case, *left_deleted_ret
 * will be set.
 */
static AVL_INLINE struct avl_tree_node *
avl_handle_subtree_shrink(struct avl_tree_node ** const root_ptr,
			  struct avl_tree_node *parent,
			  const int sign,
			  bool * const left_deleted_ret)
{
	struct avl_tree_node *node;
	int old_balance_factor, new_balance_factor;

	old_balance_factor = avl_get_balance_factor(parent);

	if (old_balance_factor == 0) {
		/* Prior to the deletion, the subtree rooted at
		 * @parent was perfectly balanced.  It's now
		 * unbalanced by 1, but that's okay and its height
		 * hasn't changed.  Nothing more to do.  */
		avl_adjust_balance_factor(parent, sign);
		return NULL;
	}

	new_balance_factor = old_balance_factor + sign;

	if (new_balance_factor == 0) {
		/* The subtree rooted at @parent is now perfectly
		 * balanced, whereas before the deletion it was
		 * unbalanced by 1.  Its height must have decreased
		 * by 1.  No rotation is needed at this location,
		 * but continue up the tree.  */
		avl_adjust_balance_factor(parent, sign);
		node = parent;
	} else {
		/* @parent is too left-heavy (new_balance_factor == -2) or
		 * too right-heavy (new_balance_factor == +2).  */

		node = avl_get_child(parent, sign);

		/* The rotations below are similar to those done during
		 * insertion (see avl_handle_subtree_growth()), so full
		 * comments are not provided.  The only new case is the
		 * one where @node has a balance factor of 0, and that is
		 * commented.  */

		if (sign * avl_get_balance_factor(node) >= 0) {

			avl_rotate(root_ptr, parent, -sign);

			if (avl_get_balance_factor(node) == 0) {
				/*
				 * @node (B below) is perfectly balanced.
				 *
				 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
				 * The comment, diagram, and equations
				 * below assume sign < 0.  The other case
				 * is symmetric!
				 * @@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@@
				 *
				 * Do a clockwise rotation rooted at
				 * @parent (A below):
				 *
				 *           A              B
				 *          / \           /   \
				 *         B   C?  =>    D     A
				 *        / \           / \   / \
				 *       D   E         F?  G?E   C?
				 *      / \
				 *     F?  G?
				 *
				 * Before the rotation:
				 *	balance(A) = -2
				 *	balance(B) =  0
				 * Let x = height(C).  Then:
				 *	height(B) = x + 2
				 *	height(D) = x + 1
				 *	height(E) = x + 1
				 *	max(height(F), height(G)) = x.
				 *
				 * After the rotation:
				 *	height(D) = max(height(F), height(G)) + 1
				 *		  = x + 1
				 *	height(A) = max(height(E), height(C)) + 1
				 *		  = max(x + 1, x) + 1 = x + 2
				 *	balance(A) = -1
				 *	balance(B) = +1
				 */

				/* A: -2 => -1 (sign < 0)
				 * or +2 => +1 (sign > 0)
				 * No change needed --- that's the same as
				 * old_balance_factor.  */

				/* B: 0 => +1 (sign < 0)
				 * or 0 => -1 (sign > 0)  */
				avl_adjust_balance_factor(node, -sign);

				/* Height is unchanged; nothing more to do.  */
				return NULL;
			} else {
				avl_adjust_balance_factor(parent, -sign);
				avl_adjust_balance_factor(node, -sign);
			}
		} else {
			node = avl_do_double_rotate(root_ptr, node,
						    parent, -sign);
		}
	}
	parent = avl_get_parent(node);
	if (parent)
		*left_deleted_ret = (node == parent->left);
	return parent;
}

/* Swaps node X, which must have 2 children, with its in-order successor, then
 * unlinks node X.  Returns the parent of X just before unlinking, without its
 * balance factor having been updated to account for the unlink.  */
static AVL_INLINE struct avl_tree_node *
avl_tree_swap_with_successor(struct avl_tree_node **root_ptr,
			     struct avl_tree_node *X,
			     bool *left_deleted_ret)
{
	struct avl_tree_node *Y, *ret;

	Y = X->right;
	if (!Y->left) {
		/*
		 *     P?           P?           P?
		 *     |            |            |
		 *     X            Y            Y
		 *    / \          / \          / \
		 *   A   Y    =>  A   X    =>  A   B?
		 *      / \          / \
		 *    (0)  B?      (0)  B?
		 *
		 * [ X unlinked, Y returned ]
		 */
		ret = Y;
		*left_deleted_ret = false;
	} else {
		struct avl_tree_node *Q;

		do {
			Q = Y;
			Y = Y->left;
		} while (Y->left);

		/*
		 *     P?           P?           P?
		 *     |            |            |
		 *     X            Y            Y
		 *    / \          / \          / \
		 *   A   ...  =>  A  ...   =>  A  ...
		 *       |            |            |
		 *       Q            Q            Q
		 *      /            /            /
		 *     Y            X            B?
		 *    / \          / \
		 *  (0)  B?      (0)  B?
		 *
		 *
		 * [ X unlinked, Q returned ]
		 */

		Q->left = Y->right;
		if (Q->left)
			avl_set_parent(Q->left, Q);
		Y->right = X->right;
		avl_set_parent(X->right, Y);
		ret = Q;
		*left_deleted_ret = true;
	}

	Y->left = X->left;
	avl_set_parent(X->left, Y);

	Y->parent_balance = X->parent_balance;
	avl_replace_child(root_ptr, avl_get_parent(X), X, Y);

	return ret;
}

/*
 * Removes an item from the specified AVL tree.
 *
 * @root_ptr
 *	Location of the AVL tree's root pointer.  Indirection is needed
 *	because the root node may change if the tree needed to be rebalanced
 *	because of the deletion or if @node was the root node.
 *
 * @node
 *	Pointer to the `struct avl_tree_node' embedded in the item to
 *	remove from the tree.
 *
 * Note: This function *only* removes the node and rebalances the tree.
 * It does not free any memory.
 */
void
avl_tree_remove(struct avl_tree_node **root_ptr, struct avl_tree_node *node)
{
	struct avl_tree_node *parent;
	bool left_deleted = false;

	if (node->left && node->right) {
		/* @node is fully internal, with two children.  Swap it
		 * with its in-order successor (which must exist in the
		 * right subtree of @node and can have, at most, a right
		 * child), then unlink @node.  */
		parent = avl_tree_swap_with_successor(root_ptr, node,
						      &left_deleted);
		/* @parent is now the parent of what was @node's in-order
		 * successor.  It cannot be NULL, since @node itself was
		 * an ancestor of its in-order successor.
		 * @left_deleted has been set to %true if @node's
		 * in-order successor was the left child of @parent,
		 * otherwise %false.  */
	} else {
		struct avl_tree_node *child;

		/* @node is missing at least one child.  Unlink it.  Set
		 * @parent to @node's parent, and set @left_deleted to
		 * reflect which child of @parent @node was.  Or, if
		 * @node was the root node, simply update the root node
		 * and return.  */
		child = node->left ? node->left : node->right;
		parent = avl_get_parent(node);
		if (parent) {
			if (node == parent->left) {
				parent->left = child;
				left_deleted = true;
			} else {
				parent->right = child;
				left_deleted = false;
			}
			if (child)
				avl_set_parent(child, parent);
		} else {
			if (child)
				avl_set_parent(child, parent);
			*root_ptr = child;
			return;
		}
	}

	/* Rebalance the tree.  */
	do {
		if (left_deleted)
			parent = avl_handle_subtree_shrink(root_ptr, parent,
							   +1, &left_deleted);
		else
			parent = avl_handle_subtree_shrink(root_ptr, parent,
							   -1, &left_deleted);
	} while (parent);
}
