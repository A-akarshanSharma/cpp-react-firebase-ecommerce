// Store only checkout intent, never credentials. Keep the key across ambiguous
// failures and page reloads so the backend can return an already-committed order.
export function savedCheckoutAttempt(uid) {
  try {
    return JSON.parse(sessionStorage.getItem(`studio-checkout:${uid}`) || 'null')
  } catch {
    return null
  }
}
export function checkoutAttempt(
  uid,
  cartVersion,
  shippingAddress,
  shippingMethod,
  quoteVersion,
  pickupContact,
) {
  const storageKey = `studio-checkout:${uid}`
  const saved = savedCheckoutAttempt(uid)
  if (
    saved &&
    typeof saved.key === 'string' &&
    (saved.cartVersion === undefined || typeof saved.cartVersion === 'string')
  )
    return saved
  const attempt = {
    key: crypto.randomUUID(),
    ...(quoteVersion ? { quoteVersion } : {}),
    ...(cartVersion ? { cartVersion } : {}),
    ...(shippingAddress ? { shippingAddress } : {}),
    ...(pickupContact ? { pickupContact } : {}),
    ...(shippingMethod
      ? { shippingMethodId: shippingMethod.id, shippingFeeMinor: shippingMethod.feeMinor }
      : {}),
  }
  try {
    sessionStorage.setItem(storageKey, JSON.stringify(attempt))
  } catch {
    throw new Error('Enable session storage in your browser to place an order safely.')
  }
  return attempt
}
export function clearCheckoutAttempt(uid) {
  try {
    sessionStorage.removeItem(`studio-checkout:${uid}`)
  } catch {
    /* The completed key remains safe to replay. */
  }
}
