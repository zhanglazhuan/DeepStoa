import { Link } from 'react-router-dom'

export default function CTABanner() {
  return (
    <section className="mx-6 mb-24 rounded-sm bg-neutral-900 px-8 py-28 text-center text-white lg:mx-12 lg:px-12">
      <h2 className="fluid-heading-lg mb-6">Try it in your classroom</h2>
      <p className="mx-auto mb-10 max-w-md text-base leading-relaxed text-neutral-400">
        Be among the first to experience DeepStoa. Join the waitlist today.
      </p>
      <Link
        to="/pre-order"
        className="inline-block rounded-full bg-white px-8 py-4 text-base font-medium text-neutral-900 transition-colors hover:bg-neutral-200"
      >
        Join waitlist
      </Link>
    </section>
  )
}
