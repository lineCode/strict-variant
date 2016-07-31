//  (C) Copyright 2016 Christopher Beck

//  Distributed under the Boost Software License, Version 1.0. (See accompanying
//  file LICENSE or copy at http://www.boost.org/LICENSE_1_0.txt)

#pragma once

#include <safe_variant/mpl/find_with.hpp>
#include <safe_variant/mpl/std_traits.hpp>
#include <safe_variant/mpl/typelist.hpp>
#include <safe_variant/variant_fwd.hpp>

#include <type_traits>
#include <utility>

namespace safe_variant {

namespace detail {

/***
 * Dispatch mechanism:
 *   These two templates are used to dispatch visitor objects to
 *   the variant's storage. The problem that is being solved here is,
 *   how do we actually recover type information from the runtime
 *   value of `m_which`, so that we can call the appropriate overload
 *   of a visitor to the variant.
 */

/// Function which evaluates a visitor against a variant's internals
/// Reinteprets' the "storage" pointer as a value of type T or const T,
/// then applies the visitor object.
template <typename T, typename Internal, typename Storage, typename Visitor, typename... Args>
auto
visitor_caller(Storage && storage, Visitor && visitor, Args &&... args)
  -> decltype(std::forward<Visitor>(std::declval<Visitor>())(
    get_value(std::forward<Storage>(std::declval<Storage>()).template as<T>(), Internal()),
    std::forward<Args>(std::declval<Args>())...)) {
  auto & val = get_value(std::forward<Storage>(storage).template as<T>(), Internal());
  return std::forward<Visitor>(visitor)(val, std::forward<Args>(args)...);
}

/// Trait which figures out what the return type of visitor caller is
template <typename T, typename Internal, typename Storage, typename Visitor, typename... Args>
struct visitor_caller_return_type {
  using type = decltype(visitor_caller<T, Internal, Storage, Args...>(
    std::forward<Storage>(std::declval<Storage>()), std::forward<Visitor>(std::declval<Visitor>()),
    std::forward<Args>(std::declval<Args>())...));
};

/// Helper which figures out the return type of multiple visitor calls
/// It's better for this to be separate of the dispatch mechanism, because
/// std::common_type can technically be order dependent. It's confusing if the
/// return type can change depending on the dispatch strategy used, and it
/// simplifies the dispatch code to only have to implement this once.
template <typename Internal, typename Storage, typename Visitor, typename... Args>
struct return_typer {
  template <typename T>
  struct helper {
    using type = typename visitor_caller_return_type<T, Internal, Storage, Visitor, Args...>::type;
  };
};

/// Helper object which dispatches a visitor object to the appropriate
/// interpretation of our storage value, based on value of "which".
///
/// The solution here is that for each possible value of `which`, we create an
/// appropriate function using the above function template, and store them each
/// in an array, using parameter pack expansion. (A little 'jump table'.)
///
/// Then we dereference the array at index `m_which` and call that function.
/// This means we pick out the right function very quickly, but it may not be
/// inlined by the compiler even if it is small.
template <typename return_t, typename Internal, typename... AllTypes>
struct jumptable_dispatch {
  template <typename Storage, typename Visitor, typename... Args>
  return_t operator()(const unsigned int which, Storage && storage, Visitor && visitor,
                      Args &&... args)

  {
    using whichCaller =
      typename mpl::remove_reference_t<Visitor>::result_type (*)(Storage, Visitor, Args...);

    static whichCaller callers[sizeof...(AllTypes)] = {
      &visitor_caller<AllTypes, Internal, Storage, Visitor, Args...>...};

    // ASSERT(which < static_cast<unsigned int>(sizeof...(AllTypes)));

    return (*callers[which])(std::forward<Storage>(storage), std::forward<Visitor>(visitor),
                             std::forward<Args>(args)...);
  }
};

/// Same as the above, but we use a different strategy based on a binary tree,
/// and repeated testing of the "which" value.
///
/// The idea is that if there are multiple types, we just test if we are looking
/// in the first half or the second half, and branch to two different
/// instantiations of the binary_search_dispatch object as appropriate.
///
/// When arranged this way, the compiler can always inline the visitor calls,
/// and so for variants with few types this may be significantly faster.
///
/// The "which" value is not changed even as the type list gets smaller,
/// instead, the "base" value is increased.
template <typename return_t, typename Internal, unsigned int, typename /*Type List */>
struct binary_search_dispatch;

template <typename return_t, typename Internal, unsigned int base, typename T>
struct binary_search_dispatch<return_t, Internal, base, TypeList<T>> {
  template <typename Storage, typename Visitor, typename... Args>
  return_t operator()(const unsigned int which, Storage && storage, Visitor && visitor,
                      Args &&... args) {
    // ASSERT(which == base);
    static_cast<void>(which);

    return visitor_caller<T, Internal, Storage, Visitor, Args...>(
      std::forward<Storage>(storage), std::forward<Visitor>(visitor), std::forward<Args>(args)...);
  }
};

template <typename return_t, typename Internal, unsigned int base, typename T1, typename T2,
          typename... Types>
struct binary_search_dispatch<return_t, Internal, base, TypeList<T1, T2, Types...>> {
  typedef TypeList<T1, T2, Types...> TList;
  typedef Subdivide<TList> Subdiv;
  typedef typename Subdiv::L TL;
  typedef typename Subdiv::R TR;
  static constexpr unsigned int split_point = base + static_cast<unsigned int>(TL::size);

  template <typename Storage, typename Visitor, typename... Args>
  return_t operator()(const unsigned int which, Storage && storage, Visitor && visitor,
                      Args &&... args) {

    if (which < split_point) {
      return binary_search_dispatch<return_t, Internal, base, TL>{}(
        which, std::forward<Storage>(storage), std::forward<Visitor>(visitor),
        std::forward<Args>(args)...);
    } else {
      return binary_search_dispatch<return_t, Internal, split_point, TR>{}(
        which, std::forward<Storage>(storage), std::forward<Visitor>(visitor),
        std::forward<Args>(args)...);
    }
  }
};

/// Choose the jumptable dispatch strategy when the number of types is > switch
/// point
/// choose the binary search dispatch for less than that.
/// Tentatively choosing 4 for switch point, potentially 8 is better... ?
/// Needs more rigorous benchmarking
template <typename Internal, typename... AllTypes>
struct visitor_dispatch {
  static constexpr unsigned int switch_point = 4;

  /*
  template <typename Visitor, typename Enable = void>
  struct has_return_typedef {
    static constexpr bool value = false;
  };

  template <typename Visitor>
  struct has_return_typedef<Visitor, decltype(static_cast<typename
  mpl::remove_reference_t<Visitor>::return_type *>(nullptr), void())> {
    static constexpr bool value = true;
    using type = typename mpl::remove_reference_t<Visitor>::return_type;
  };
  */

  template <typename Storage, typename Visitor, typename... Args>
  auto operator()(const unsigned int which, Storage && storage, Visitor && visitor, Args &&... args)
    ->
    typename mpl::typelist_fwd<mpl::common_type_t,
                               typename mpl::typelist_map<return_typer<Internal, Storage, Visitor,
                                                                       Args...>::template helper,
                                                          TypeList<AllTypes...>>::type>::type {
    using return_t =
      typename mpl::typelist_fwd<mpl::common_type_t,
                                 typename mpl::typelist_map<return_typer<Internal, Storage, Visitor,
                                                                         Args...>::template helper,
                                                            TypeList<AllTypes...>>::type>::type;

    // using chosen_dispatch_t = jumptable_dispatch<return_t, Internal, AllTypes...>;

    // using chosen_dispatch_t =
    //  typename std::conditional<(sizeof...(AllTypes) > switch_point),
    //  jumptable_dispatch<return_t, Internal, AllTypes...>,
    //                            binary_search_dispatch<return_t, Internal, 0,
    //                            TypeList<AllTypes...>>>::type;

    using chosen_dispatch_t = binary_search_dispatch<return_t, Internal, 0, TypeList<AllTypes...>>;

    return chosen_dispatch_t{}(which, std::forward<Storage>(storage),
                               std::forward<Visitor>(visitor), std::forward<Args>(args)...);
  }

  /*
    template <typename Storage, typename Visitor, typename... Args>
    auto operator()(const unsigned int which, Storage && storage,
                    Visitor && visitor, Args &&... args) -> typename
    has_return_typedef<Visitor>::type {
      using return_t = typename has_return_typedef<Visitor>::type;

      // using chosen_dispatch_t = jumptable_dispatch<return_t, Internal, AllTypes...>;

      // using chosen_dispatch_t =
      //  typename std::conditional<(sizeof...(AllTypes) > switch_point),
      //  jumptable_dispatch<return_t, Internal, AllTypes...>,
      //                            binary_search_dispatch<return_t, Internal, 0,
    TypeList<AllTypes...>>>::type;

      using chosen_dispatch_t = binary_search_dispatch<return_t, Internal, 0,
    TypeList<AllTypes...>>;

      return chosen_dispatch_t{}(which,
                                 std::forward<Storage>(storage), std::forward<Visitor>(visitor),
                                 std::forward<Args>(args)...);
    }
  */
};

} // end namespace detail

} // end namespace safe_variant